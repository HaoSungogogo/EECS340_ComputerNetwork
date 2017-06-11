#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>


#include <iostream>

#include "Minet.h"
#include "tcpstate.h"

//Initial Parameters
#define INIT_TIMER 10 //Seconds
#define INIT_TIMETRIES 3
#define LOSS_FACTOR 8

//Settings
#define PACKET_LOSS 0
#define RESEND_LISTEN 1
#define RESEND_CONNECT 1

using std::cout;
using std::endl;
using std::cerr;
using std::string;

enum msgstate {
				SendSYN      	= 0,
	      SendACK      	= 1,
	      SendFIN    		= 2,
	      SendRST  		  = 3,
	      SendSYN_ACK   = 4,
				Send          = 5 };

int SendEmptyPacket(MinetHandle handle) {
	Packet p;
	MinetSend(handle, p);
}

void PushIPHeader(ConnectionToStateMapping<TCPState> &mapping, Packet  *p, int datalen) {
	IPHeader iph;
	iph.SetSourceIP(mapping.connection.src);
	iph.SetDestIP(mapping.connection.dest);
	iph.SetProtocol(mapping.connection.protocol);
	iph.SetTotalLength(TCP_HEADER_BASE_LENGTH + IP_HEADER_BASE_LENGTH + datalen);
	p->PushFrontHeader(iph);
}

void PushTCPHeader(ConnectionToStateMapping<TCPState> &mapping, Packet *p, int msgstate) {
	unsigned char flags = 0;
	TCPHeader tcph;
	
	switch(msgstate) {
		case SendSYN: {
			SET_SYN(flags);
		} break;
		case SendACK: {
			SET_ACK(flags);
		} break;
		case SendFIN: {
			SET_FIN(flags);
			SET_ACK(flags);
		} break;
		case SendRST: {
			SET_RST(flags);
		} break;
		case SendSYN_ACK: {
			SET_SYN(flags);
			SET_ACK(flags);
		} break;
		case Send:
			break;
	}

	tcph.SetSourcePort(mapping.connection.srcport, *p);
	tcph.SetDestPort(mapping.connection.destport, *p);
	tcph.SetFlags(flags, *p);
	if(mapping.state.GetState() == ESTABLISHED)
		tcph.SetAckNum(mapping.state.GetLastRecvd(), *p);
	else
		tcph.SetAckNum(mapping.state.GetLastRecvd() + 1, *p);
	tcph.SetSeqNum(mapping.state.GetLastSent() + 1, *p);
	tcph.SetHeaderLen(TCP_HEADER_BASE_LENGTH / 4, *p);
	tcph.SetWinSize(mapping.state.GetRwnd(), *p);
	tcph.SetUrgentPtr(0, *p);
	tcph.RecomputeChecksum(*p);

	p->PushBackHeader(tcph);
	unsigned int seq, ack;
	tcph.GetSeqNum(seq);
	tcph.GetAckNum(ack);
	cerr << "PushTCPHeaderSeqNum : " << seq << endl << "PushTCPHeaderAckNum : " << ack << endl;
}

int SendPacket(MinetHandle handle, ConnectionToStateMapping<TCPState> &mapping, int msgstate) {
	int count = 0;
send:
	size_t size = 0;
	size_t offset = mapping.state.last_sent + 1 - mapping.state.last_acked;
	if(offset > mapping.state.SendBuffer.GetSize())
		offset = mapping.state.SendBuffer.GetSize();

	Buffer payload;
	payload.Clear();
	
	if((mapping.state.GetState() == ESTABLISHED) || (mapping.state.GetState() == CLOSE_WAIT)) {
		mapping.state.rwnd = (mapping.state.N > mapping.state.SendBuffer.GetSize() ? (mapping.state.SendBuffer.GetSize()) : (mapping.state.N));
		size = mapping.state.rwnd- offset;
		size = (TCP_MAXIMUM_SEGMENT_SIZE > size) ? (size) : (TCP_MAXIMUM_SEGMENT_SIZE);
		payload = mapping.state.SendBuffer.Extract(offset, size);
	}

	Packet p(payload);
	
	if((mapping.state.GetState() == ESTABLISHED) || (mapping.state.GetState() == CLOSE_WAIT)) {
		mapping.state.SendBuffer.Insert(payload, offset);
	}

	cerr << "N = " << mapping.state.N << endl << "BufferLen = " << mapping.state.SendBuffer.GetSize() << endl << "Size = " << size << endl << "Offset = " << offset << endl;

	PushIPHeader(mapping, &p, size);
	PushTCPHeader(mapping, &p, msgstate);

	#if PACKET_LOSS
	//Packet Loss
	if(((random()%LOSS_FACTOR) == 0) && (mapping.state.GetState() == ESTABLISHED)) {
		SendEmptyPacket(handle);
		cerr << endl << "-----Packet Loss!-----" << endl << endl;
	} else {
		MinetSend(handle, p);
		TCPHeader tcph=p.FindHeader(Headers::TCPHeader);
		cerr << "Send packet : " << p << endl;
		cerr << "With TCPHeader : " << tcph << endl;
	}
	#else
	MinetSend(handle, p);
	TCPHeader tcph=p.FindHeader(Headers::TCPHeader);
	cerr << "Send packet : " << p << endl;
	cerr << "With TCPHeader : " << tcph << endl;
	#endif

	if((mapping.state.GetState() == ESTABLISHED) || (mapping.state.GetState() == CLOSE_WAIT)) {
		mapping.state.last_sent += size;
	} else {
		mapping.state.last_sent += 1;
	}

	cerr << "last_sent refreshed:" << mapping.state.last_sent << endl;

	if((mapping.state.SendBuffer.GetSize() - size - offset > 0) && (offset < mapping.state.N)) {
		count += size;
		goto send;
	}
	count += size;
	return count;
}

int ReceivePacket(MinetHandle handle, Packet *p, TCPHeader *tcph, IPHeader *iph,Connection *con, Buffer *data, unsigned char *flags, unsigned int *SeqNum, unsigned int *AckNum, unsigned short *len)
{
	int ok = 0;
	unsigned char IPHeaderLen;
	unsigned short IPPacketLen;
	unsigned char TCPHeaderLen;
	
	MinetReceive(handle, *p);

	unsigned tcphlen=TCPHeader::EstimateTCPHeaderLength(*p);
	p->ExtractHeaderFromPayload<TCPHeader>(tcphlen);
	*iph=p->FindHeader(Headers::IPHeader);
	*tcph=p->FindHeader(Headers::TCPHeader);

	if (!tcph->IsCorrectChecksum(*p)||!iph->IsChecksumCorrect())
		cerr << "forwarding packet to sock even though checksum failed" << endl;
				
	Connection c;
	//process ipheader
	iph->GetDestIP(c.src);
	iph->GetSourceIP(c.dest);
	iph->GetProtocol(c.protocol);
	iph->GetHeaderLength(IPHeaderLen);
	iph->GetTotalLength(IPPacketLen);
	//process tcpheader
	tcph->GetDestPort(c.srcport);
	tcph->GetSourcePort(c.destport);
	tcph->GetFlags(*flags);
	tcph->GetSeqNum(*SeqNum);
	tcph->GetAckNum(*AckNum);
	tcph->GetHeaderLen(TCPHeaderLen);
	//data
	*len = IPPacketLen - IPHeaderLen*4 - TCPHeaderLen*4;
	*data = p->GetPayload().ExtractFront(*len);
	//connection
	*con = c;

	ok = 1;
	return ok;
}

int main(int argc, char *argv[])
{
  MinetHandle mux, sock;

	ConnectionList<TCPState> clist;

  MinetInit(MINET_TCP_MODULE);
	
  mux=MinetIsModuleInConfig(MINET_IP_MUX) ? MinetConnect(MINET_IP_MUX) : MINET_NOHANDLE;
  sock=MinetIsModuleInConfig(MINET_SOCK_MODULE) ? MinetAccept(MINET_SOCK_MODULE) : MINET_NOHANDLE;

  if (MinetIsModuleInConfig(MINET_IP_MUX) && mux==MINET_NOHANDLE) {
    MinetSendToMonitor(MinetMonitoringEvent("\nCan't connect to mux\n"));
    return -1;
  }

  if (MinetIsModuleInConfig(MINET_SOCK_MODULE) && sock==MINET_NOHANDLE) {
    MinetSendToMonitor(MinetMonitoringEvent("\nCan't accept from sock module\n"));
    return -1;
  }

  MinetSendToMonitor(MinetMonitoringEvent("tcp_module handling TCP traffic\n"));
	
	double timeout = -1;
  MinetEvent event;

  while (MinetGetNextEvent(event, timeout)==0) {
		cerr << endl << endl << "NEW EVENT---------------------------" << endl << endl;
    // if we received an unexpected type of event, print error
    if (event.eventtype!=MinetEvent::Dataflow || event.direction!=MinetEvent::IN) {
			if (event.eventtype == MinetEvent::Timeout) {
				ConnectionList<TCPState>::iterator timerSetter = clist.FindEarliest();
				if (timerSetter != clist.end()) {
					switch(timerSetter->state.GetState()) {
						case ESTABLISHED: {
							if(timerSetter->state.SendBuffer.GetSize() > 0) {
								cerr << "Data transfer timeout" << endl;
								timerSetter->state.last_sent = timerSetter->state.last_acked - 1;
								SendPacket(mux, *timerSetter, SendACK);
							}
							timeout = timerSetter->timeout;
						}	break;
						
						case CLOSE_WAIT: {
							if(timerSetter->state.SendBuffer.GetSize() > 0) {
								cerr << "Data transfer timeout" << endl;
								timerSetter->state.last_sent = timerSetter->state.last_acked - 1;
								SendPacket(mux, *timerSetter, SendACK);
							}
							timeout = timerSetter->timeout;
						}	break;

						case SYN_SENT: {
							cerr << "SYN_SENT timeout" << endl;
							timerSetter->state.SetState(CLOSED);
							cerr << "Go to state : CLOSED" << endl;
							timeout = -1;
						}	break;

						case SYN_RCVD: {
							cerr << "SYN_RCVD timeout" << endl;
							SendPacket(mux, *timerSetter, SendRST);
							timerSetter->state.SetState(CLOSED);
							cerr << "Go to state : CLOSED" << endl;
							timeout = -1;
						}	break;

						case TIME_WAIT: {
							cerr << "TIME_WAIT timeout" << endl;
							timerSetter->state.tmrTries -= 1;
							if(timerSetter->state.tmrTries == 1) {
								timerSetter->state.SetState(CLOSED);
								cerr << "Go to state : CLOSED" << endl;
								timeout = -1;
							}
						}	break;
			
						default:
							timeout = -1;
					}
				}
			}
    } else {
      //  Data from the IP layer below  //
      if (event.handle==mux) {
				Packet p;
				Connection c;
				IPHeader iph;
		    TCPHeader tcph;
				Buffer buffer;
				unsigned char flags;
				unsigned int SeqNum;
				unsigned int AckNum;
				unsigned short len;

				ReceivePacket(mux, &p, &tcph, &iph, &c, &buffer, &flags, &SeqNum, &AckNum, &len);

				ConnectionList<TCPState>::iterator cs = clist.FindMatching(c);

				timeout = cs->timeout;
				
				if (cs!=clist.end()) {
						cerr << "Received TCP Packet: IP Header is "<<iph<<" and ";
        		cerr << "TCP Header is "<<tcph << endl;
						cerr << "Received Seq = " << SeqNum << endl;
						cerr << "Received Ack = " << AckNum << endl;
        		cerr << "Checksum is " << (tcph.IsCorrectChecksum(p) ? "VALID" : "INVALID") << endl;
						cerr << "switch to state : " << cs->state.GetState() << endl;
					  
						if(len < 1)//patch for len in states which isn't ESTABLISHED
							len = 1;
						len -= 1;

						if((SeqNum == cs->state.last_recvd + 1) || (cs->state.GetState() != ESTABLISHED)) {
							cs->state.last_recvd = SeqNum + len;
							cerr << "last_recvd refreshed:" << cs->state.last_recvd << endl;
						}
						
						if(cs->state.last_acked < AckNum) {
							cs->state.SendBuffer.Erase(0, AckNum - cs->state.last_acked);
							cs->state.last_acked = AckNum;
							cerr << "last_acked refreshed:" << cs->state.last_acked << endl;
							if(cs->state.last_sent < (cs->state.last_acked - 1))
								cs->state.last_sent = cs->state.last_acked - 1;
						}
						
						switch(cs->state.GetState()) {
							case CLOSED:
								cerr << "CLOSED" << endl;
								break;

							case LISTEN: {
								if(IS_SYN(flags)) {
									cs->connection = c;

									//Resend in LISTEN handler
									#if RESEND_LISTEN
									SendPacket(mux, *cs, SendSYN_ACK);
									for(int i; i < 0xffffff; i++) {
							
									}
									cs->state.last_sent -= 1;
									#endif
									SendPacket(mux, *cs, SendSYN_ACK);

									cs->state.SetState(SYN_RCVD);
									cerr << "Go to state : SYN_RCVD" << endl;
									cs->bTmrActive = true;
									timeout = cs->timeout;
									SockRequestResponse repl;
									repl.type = WRITE;
									repl.connection = c;
									repl.bytes = 0;
									repl.error = EOK;
									MinetSend(sock,repl);
								}
							} break;

							case SYN_RCVD: {
								if(IS_ACK(flags)) {
									cs->state.SetState(ESTABLISHED);
									cs->state.last_recvd -= 1;
									cerr << "Go to state : ESTABLISHED" << endl;
								}
								if(IS_RST(flags)) {
									cs->state.SetState(LISTEN);
									cerr << "Go to state : LISTEN" << endl;
								}
							} break;

							case SYN_SENT: {
								if(IS_SYN(flags) && !IS_ACK(flags)) {
									SendPacket(mux, *cs, SendSYN_ACK);
									cs->state.SetState(SYN_RCVD);
									cerr << "Go to state : SYN_RCVD" << endl;
								}
								if(IS_SYN(flags) && IS_ACK(flags)) {
									SendPacket(mux, *cs, SendACK);
									cs->state.SetState(ESTABLISHED);
									cerr << "Go to state : ESTABLISHED" << endl;
									cs->state.last_sent -= 1;
									SockRequestResponse repl;
									repl.type = WRITE;
									repl.connection = c;
									repl.bytes = 0;
									repl.error = EOK;
									MinetSend(sock,repl);
								}
							} break;

							case ESTABLISHED: {
								if(len != 0){
									cerr << "ESTABLISH Send ACK" << endl;
									SockRequestResponse repl;
									repl.type = WRITE;
									repl.connection = c;
									repl.data = buffer;
									repl.bytes = len + 1;
									repl.error = EOK;
									cs->state.RecvBuffer.AddBack(buffer);
									MinetSend(sock,repl);
									cs->state.last_recvd += 1;
									SendPacket(mux, *cs, SendACK);
									cs->state.last_recvd -= 1;
								} else {
									if(IS_ACK(flags) && !IS_FIN(flags)){
										cerr << "ESTABLISH Receive ACK" << endl;
										if(cs->state.SendBuffer.GetSize() > 0)
											SendPacket(mux, *cs, SendACK);
									} else if(IS_FIN(flags)) {
										cs->state.last_recvd += 1;//datalen = 0 but we should increase the acknumber by 1 leaving ESTABLISH
										SendPacket(mux, *cs, SendACK);
										SockRequestResponse repl;
										repl.type = WRITE;
										repl.connection = c;
										repl.data = buffer;
										repl.bytes = 0;
										repl.error = EOK;
										MinetSend(sock,repl);
										cs->state.SetState(CLOSE_WAIT);
										cerr << "Go to state : CLOSE_WAIT" << endl;
									}
								}
							} break;

							case FIN_WAIT1: {
								if(IS_ACK(flags) && (!IS_ACK(flags))) {
									cs->state.SetState(FIN_WAIT2);
									cerr << "Go to state : FIN_WAIT2" << endl;
								} else if(IS_FIN(flags) && IS_ACK(flags)) {
									SendPacket(mux, *cs, SendACK);
									cs->state.SetState(TIME_WAIT);
									cerr << "Go to state : TIME_WAIT" << endl;
								}
							} break;

							case CLOSING: {
								if(IS_ACK(flags)) {
									cs->state.SetState(TIME_WAIT);
									cerr << "Go to state : TIME_WAIT" << endl;
								}
							} break;

							case LAST_ACK: {
								if(IS_ACK(flags)) {
									cs->state.SetState(CLOSED);
									cerr << "Go to state : CLOSED" << endl;
									timeout = -1;
								}
							} break;

							case FIN_WAIT2: {
								if(IS_FIN(flags)) {
									SendPacket(mux, *cs, SendACK);
									cs->state.SetState(TIME_WAIT);
									cerr << "Go to state : TIME_WAIT" << endl;
								}
							} break;

							case TIME_WAIT:
								cs->state.SetState(CLOSED);
								cerr << "Go to state : CLOSED" << endl;
								timeout = -1;
								break;

							case CLOSE_WAIT:
								cerr << "CLOSE_WAIT RecvACK" << endl;

							default:
								cerr << "Invalid State" << endl;
						}
				} else {
					cerr << "Unknown port, sending ICMP error message" << endl;
					IPAddress source;
			 		iph.GetSourceIP(source);
			 		ICMPPacket error(source,DESTINATION_UNREACHABLE,PORT_UNREACHABLE,p);
					cerr << "ICMP error message has been sent to host" << endl;
					MinetSend(mux, error);
				}
      }
			
      //  Data from the Sockets layer above  //
      if (event.handle==sock) {
        SockRequestResponse ReqFromSock, repl;
        MinetReceive(sock,ReqFromSock);
				
				switch (ReqFromSock.type) {
					case CONNECT: { // Active open to remote
							cerr <<"\nActiveOpen\n";
							Time initTimeout(INIT_TIMER);
							TCPState tcpstate( random(), SYN_SENT, INIT_TIMETRIES);
							tcpstate.SendBuffer.Clear();
							tcpstate.RecvBuffer.Clear();
							ConnectionToStateMapping<TCPState> new_connection(ReqFromSock.connection, initTimeout, tcpstate, true);
							new_connection.bTmrActive = true;

							//Resend in CONNECT handler
							#if RESEND_CONNECT
							SendPacket(mux, new_connection, SendSYN);
							for(int i; i < 0xffffff; i++) {
							
							}
							new_connection.state.last_sent -= 1;
							#endif
							SendPacket(mux, new_connection, SendSYN);

							clist.push_front(new_connection);
							timeout = new_connection.timeout;
							repl.type=STATUS;
							repl.connection=ReqFromSock.connection;
							repl.error=EOK;
							MinetSend(sock,repl);
						} break;
						
					case ACCEPT: { // Passive open from remote
							cerr <<"\nPassiveOpen\n";
							Time initTimeout(INIT_TIMER);
							TCPState tcpstate( random(), LISTEN, INIT_TIMETRIES);
							tcpstate.SendBuffer.Clear();
							tcpstate.RecvBuffer.Clear();
							ConnectionToStateMapping<TCPState> new_connection(ReqFromSock.connection, initTimeout, tcpstate, true);
							clist.push_front(new_connection);
							repl.type=STATUS;
							repl.error=EOK;
							MinetSend(sock,repl);
						} break;
						
					case STATUS: { // Updata status
							cerr <<"\nSockStatus\n";
							ConnectionList<TCPState>::iterator cs = clist.FindMatching(ReqFromSock.connection);
							if (cs->state.GetState() == ESTABLISHED) {
		            cs->state.RecvBuffer.Erase(0, ReqFromSock.bytes);
		            if (cs->state.RecvBuffer.GetSize() > 0) {
		            	repl.type = WRITE;
		              repl.connection = cs->connection;
		              repl.error = EOK;
		              repl.bytes = cs->state.RecvBuffer.GetSize();
		              repl.data = cs->state.RecvBuffer;
		              MinetSend(sock, repl);
		            }
		          }
					  } break;

					case WRITE: { //Send TCP data
							cerr <<"\nSockWrite\n";
							ConnectionList<TCPState>::iterator cs = clist.FindMatching(ReqFromSock.connection);
							timeout = cs->timeout;
							
							if (cs!=clist.end()) {
								unsigned bytes = ReqFromSock.data.GetSize();
								Buffer buffer;
								buffer = ReqFromSock.data.ExtractFront(bytes);
								cs->state.SendBuffer.AddBack(buffer);
								unsigned sendbytes = SendPacket(mux, *cs, SendACK);
								repl.type=STATUS;
								repl.connection=ReqFromSock.connection;
								repl.bytes=bytes;
								repl.error=EOK;
								MinetSend(sock,repl);
							}
						} break;
						
					case FORWARD: { //Forward matching packets
							cerr <<"\nSockForward\n";
							repl.type=STATUS;
							repl.error=EOK;
							MinetSend(sock,repl);
						} break;
						
					case CLOSE: { //Close connection
							cerr <<"\nSockClose\n";
							ConnectionList<TCPState>::iterator cs = clist.FindMatching(ReqFromSock.connection);
							timeout = cs->timeout;
							repl.connection=ReqFromSock.connection;
							repl.type=STATUS;
							if (cs==clist.end()) {
								cerr << "Close: ENOMATCH" << endl;
								repl.error=ENOMATCH;
								MinetSend(sock,repl);
							} else {
								switch(cs->state.GetState()) {
									case SYN_RCVD: {
										SendPacket(mux, *cs, SendRST);
										cs->state.SetState(FIN_WAIT1);
									} break;
									case SYN_SENT: {
										cs->state.SetState(CLOSED);
										timeout = -1;
									} break;
									case ESTABLISHED: {
										SendPacket(mux, *cs, SendFIN);
										cs->state.SetState(FIN_WAIT1);
										cerr << "Go to state : FIN_WAIT1" << endl;
									} break;
									
									case CLOSE_WAIT: {
										SendPacket(mux, *cs, SendFIN);
										cs->state.SetState(LAST_ACK);
										cerr << "Go to state : LAST_ACK" << endl;
									} break;
								}
							}
						}
						break;
					default:
						{
							cerr <<"\nEWHAT\n";
							repl.type=STATUS;
							repl.error=EWHAT;
							MinetSend(sock,repl);
						}
				}
      }
    }
  }
  return 0;
}
