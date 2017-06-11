#include "node.h"
#include "context.h"
#include "error.h"


#if defined(LINKSTATE)
Node::Node(const unsigned n, SimulationContext *c, double b, double l) : 
    number(n), context(c), bw(b), lat(l) 
{
  table.num = n;
  cerr << "New Node:" << *this << endl;
}
#endif

#if defined(DISTANCEVECTOR)
Node::Node(const unsigned n, SimulationContext *c, double b, double l) : 
    number(n), context(c), bw(b), lat(l) 
{
  table.num = n;
  table.neighbor[n] = 0;
  table.dv[n] = map<int, record>();
  table.dv[n][n] = record(n,0);
  cerr << "New Node:" << *this << endl;
}
#endif

Node::Node() 
{ throw GeneralException(); }

Node::Node(const Node &rhs) : 
  number(rhs.number), context(rhs.context), bw(rhs.bw), lat(rhs.lat) {}

Node & Node::operator=(const Node &rhs) 
{
  return *(new(this)Node(rhs));
}

void Node::SetNumber(const unsigned n) 
{ number=n;}

unsigned Node::GetNumber() const 
{ return number;}

void Node::SetLatency(const double l)
{ lat=l;}

double Node::GetLatency() const 
{ return lat;}

void Node::SetBW(const double b)
{ bw=b;}

double Node::GetBW() const 
{ return bw;}

Node::~Node()
{}

// Implement these functions  to post an event to the event queue in the event simulator
// so that the corresponding node can recieve the ROUTING_MESSAGE_ARRIVAL event at the proper time
void Node::SendToNeighbors(const RoutingMessage *m)
{
  context->SendToNeighbors(this, m);
}

void Node::SendToNeighbor(const Node *n, const RoutingMessage *m)
{
  context->SendToNeighbor(this, n, m);
}

deque<Node*> *Node::GetNeighbors()
{
  return context->GetNeighbors(this);
}

deque<Link*> *Node::GetOutgoingLinks()
{
  return context->GetOutgoingLinks(this);
}

void Node::SetTimeOut(const double timefromnow)
{
  context->TimeOut(this,timefromnow);
}


bool Node::Matches(const Node &rhs) const
{
  return number==rhs.number;
}


#if defined(GENERIC)
void Node::LinkHasBeenUpdated(const Link *l)
{
  cerr << *this << " got a link update: "<<*l<<endl;
  //Do Something generic:
  SendToNeighbors(new RoutingMessage);
}


void Node::ProcessIncomingRoutingMessage(const RoutingMessage *m)
{
  cerr << *this << " got a routing messagee: "<<*m<<" Ignored "<<endl;
}


void Node::TimeOut()
{
  cerr << *this << " got a timeout: ignored"<<endl;
}

Node *Node::GetNextHop(const Node *destination) const
{
  return 0;
}

Table *Node::GetRoutingTable() const
{
  return new Table;
}


ostream & Node::Print(ostream &os) const
{
  os << "Node(number="<<number<<", lat="<<lat<<", bw="<<bw<<")";
  return os;
}

#endif

#if defined(LINKSTATE)
#define INFINITE 0xFFFFFFFF
#define UNDEFINE 0xFFFFFFFF

void Node::LinkHasBeenUpdated(const Link *l)
{
  cerr << *this<<": Link Update: "<<*l<<endl;
  if(!table.IsNewLink(l))
    return ;

  table.AddLink(l);
  map<int, double> distance;
  map<int, int> prevNode;
  set<int> Q(table.nodeset);
  for (set<int>::const_iterator itr = Q.begin(); itr != Q.end(); itr++) {
      distance[*itr] = INFINITE;
      prevNode[*itr] = UNDEFINE;
  }
  distance[table.num] = 0;
  while (!Q.empty()) {
      set<int>::iterator minItr = Q.begin();
      double minDistance = distance[*minItr];
      for (set<int>::const_iterator itr = Q.begin(); itr != Q.end(); itr++) {
          if (minDistance > distance[*itr]) {
              minDistance = distance[*itr];
              minItr = itr;
          }
      }
      if (minDistance == INFINITE) {
         cerr << "Dijkstra Error!" << endl;
         break;
      }
      for (map<int, double>::const_iterator itr = table.topology[*minItr].begin();
          itr != table.topology[*minItr].end(); itr++) {
          double alt = distance[*minItr] + itr->second;
          if (alt < distance[itr->first]) {
              distance[itr->first] = alt;
              prevNode[itr->first] = *minItr;
          }
      }
      Q.erase(minItr);
  }
  table.routes.clear();
  for (set<int>::const_iterator itr = table.nodeset.begin(); itr != table.nodeset.end(); itr++) {
      if (*itr == table.num) {
          continue;
      }
      int n = *itr;
      while (prevNode[n] != table.num) {
          n = prevNode[n];
      }
      table.routes[*itr] = record(n, distance[*itr]);
  }

  SendToNeighbors(new RoutingMessage(*l));
}


void Node::ProcessIncomingRoutingMessage(const RoutingMessage *m)
{
  cerr << *this << " Routing Message: "<<*m;
  LinkHasBeenUpdated(&m->link);
}

void Node::TimeOut()
{
  cerr << *this << " got a timeout: ignored"<<endl;
}

Node *Node::GetNextHop(const Node *destination) const
{
  map<int, record>::const_iterator it = table.routes.find(destination->number);
  if(it != table.routes.end())
    return new Node(table.routes.find(destination->number)->second.nextHop, 0, 0, 0);
  else
    return NULL;
}

Table *Node::GetRoutingTable() const
{
  return new Table(table);
}


ostream & Node::Print(ostream &os) const
{
  os << "Node(number="<<number<<", lat="<<lat<<", bw="<<bw<<")";
  return os;
}
#endif


#if defined(DISTANCEVECTOR)

void Node::TableUpdate(const Link *l)
{
  int dest = l->GetDest();
  double weight = l->GetLatency();
  double originalLatency = table.neighbor.find(dest) == table.neighbor.end() ? weight : table.neighbor[dest];

  map<int, map<int, record> >::iterator thisTable = table.dv.find(number);
  thisTable->second[dest] = record(dest, weight);
  table.neighbor[dest] = weight;

  cerr << "New Link:" << dest <<  " . " << weight << endl;
  cerr << "Insert map:" << (thisTable->second.find(dest))->first << " . " << (thisTable->second.find(dest))->second.latency << endl;

  //Recompute latency over new link
  for(map<int, record>::iterator thisNode = thisTable->second.begin(); thisNode != thisTable->second.end(); thisNode++) {
    if((thisNode->second.nextHop == dest) && (thisNode->first != dest))
      thisNode->second.latency = thisNode->second.latency - originalLatency + weight;
  }

  /*
  map<int, map<int, double> >::iterator neighborTable = table.dv.find(l->GetDest());

  if(neighborTable != table.dv.end()) {
    for(map<int, double>::iterator neighborVector = neighborTable->second.begin(); neighborVector != neighborTable->second.end(); neighborVector++) {
      if(thisTable->second.find(neighborVector->first) != thisTable->second.end()) {
        thisTable->second[neighborVector->first] = neighborVector->second + weight;
      }
    }
  }*/

}

void Node::TableRecompute(const Link *l)
{
  cerr << *this<<": Link Update: "<<*l<<endl;

  if(l->GetSrc() != number) {
    cerr << "src != number" << endl;
    return;
  }
  map<int, map<int, record> >::iterator thisTable = table.dv.find(number);
  //map<int, map<int, double> >::iterator neighborTable = table.dv.find(l->GetDest());

  //Processing
  cerr << "Processing" << endl;
  //TODO:Implement .find()
  for(map<int, map<int, record> >::iterator neighborTable = table.dv.begin(); neighborTable != table.dv.end(); neighborTable++) {
    cerr << "Searching neighbor node: " << neighborTable->first << endl;
    for(map<int, record>::iterator itn = neighborTable->second.begin(); itn != neighborTable->second.end(); itn++) {
      cerr << "Neighbor Table Entry:" << itn->first << " . " << itn->second.latency << endl;
      for(map<int, record>::iterator itl = thisTable->second.begin(); itl != thisTable->second.end(); itl++) {//TODO: Optimize
        if(itn->first == itl->first) {
          cerr << "My Table Entry:" << itl->first << " . " << itl->second.latency << endl;
          cerr << "itl->second(" << itl->second.latency << ") < itn->second(" << itn->second.latency << ") + "
               << "latency(" << table.neighbor[neighborTable->first] << ")\n";
          if(itl->second.latency > (itn->second.latency + table.neighbor[neighborTable->first])) {
            itl->second.latency = (itn->second.latency + table.neighbor[neighborTable->first]);
            itl->second.nextHop = neighborTable->first;
            cerr << "Modified" << endl;
          }
        }
      }
    }
  }

  //Send Message
  cerr << "Send Message" << endl;
  RoutingMessage *message = new RoutingMessage(number, thisTable->second);
  SendToNeighbors(message);

  cerr << *this<<": Link Update: "<<*l<<endl;
}

void Node::LinkHasBeenUpdated(const Link *l)
{
  switch(table.IsNewLink(l)) {
    case 0: {
      cerr << "Link Exist" << endl;
      break;
    }
    /*case 1: {
      TableUpdate(l);
      map<int, map<int, record> >::iterator thisTable = table.dv.find(number);
      RoutingMessage *message = new RoutingMessage(number, thisTable->second);
      SendToNeighbors(message);
      cerr << "Send Message:\n num = " << message->num << "\ndv = " << message->dv << endl;
      break;
    }*/
    case 1: {
      TableUpdate(l);
      TableRecompute(l);
      break;
    }
    default:
      cerr << "Invalid Link Update" << endl;
  }
}

void Node::ProcessIncomingRoutingMessage(const RoutingMessage *m)
{
  bool modified = false;
  cerr << "ProcessIncomingRoutingMessage called by Node: " << number << " and msg from Node: " << m->num << endl;
  double latency = table.neighbor[m->num];
  
  map<int, map<int, record> >::iterator thisTable = table.dv.find(number);
  map<int, record> neighborTable = m->dv;
  
  /*deque<Link*> *links = GetOutgoingLinks();
  for(deque<Link*>::iterator thisLink = links->begin(); thisLink != links->end(); thisLink++) {
    if(m->num == (*thisLink)->GetDest()) {
      latency = (*thisLink)->GetLatency();
      break;
    }
  }*/
  //if(thisTable->second.find(m->num) == thisTable->second.end()) {
    table.dv[m->num] = neighborTable;
    //thisTable->second[m->num].latency = latency;
  //}
  
  //double weight  = (thisTable->second.find(m->num))->second;
  
  for(map<int, record>::iterator itn = neighborTable.begin(); itn != neighborTable.end(); itn++) {
    cerr << "Searching for itn->first: " << itn->first << endl;
    map<int, record>::iterator itl = thisTable->second.find(itn->first);
    if(itl == thisTable->second.end()) {
      thisTable->second[itn->first] = record(m->num, itn->second.latency + latency);
      cerr << "New Dest: " << itn->first << "with latency: "<< itn->second.latency + latency << endl;
      modified = true;
    } else {
      cerr << "itl->first(" << itl->first << ") < itn->first(" << itn->first << ")" << endl;
      cerr << "itl->second(" << itl->second.latency << ") < itn->second(" 
           << itn->second.latency << ") + " << "latency(" << latency << ")\n";
      if(itl->second.latency > (itn->second.latency + latency)) {
        itl->second.latency = (itn->second.latency + latency);
        modified = true;
      }
    }
  }
    /*int tmp = itn->first;
    for(map<int, double>::iterator itl = thisTable->second.begin(); itl != thisTable->second.end(); itl++) {
      if(tmp == itl->first) {
        cerr << "itl->first(" << itl->first << ") < itn->first(" << itn->first << ")" << endl;
        cerr << "itl->second(" << itl->second << ") < itn->second(" << itn->second << ") + " << "latency(" << latency << ")\n";
        if(itl->second > (itn->second + latency)) {
          itl->second = (itn->second + latency);
          modified = true;
        }
      }
    }*/
  
  
  cerr << "Modified? " << modified << endl;

  if(modified) {
    RoutingMessage *message = new RoutingMessage(number, thisTable->second);
    SendToNeighbors(message);
  }
}

void Node::TimeOut()
{
  cerr << *this << " got a timeout: ignored"<<endl;
}


Node *Node::GetNextHop(const Node *destination) const
{
  /*Node *nextHop = NULL;
  unsigned int weight = -1;
  map<int, map<int, double> >::const_iterator thisTable = table.dv.find(number);

  deque<Node*> *neighbor = context->GetNeighbors(this);
  
  for(deque<Node*>::iterator i = neighbor->begin(); i != neighbor->end(); i++){
    map<int, map<int, double> >::const_iterator neighborTable = table.dv.find((*i)->number);
    map<int, double>::const_iterator destNode = neighborTable->second.find(destination->number);

    map<int, double>::const_iterator thisNodeToNeighbor = thisTable->second.find((*i)->number);
    if(thisNodeToNeighbor->second + destNode->second < weight){
      weight = thisNodeToNeighbor->second + destNode->second;
      nextHop = *i;
    }
  }
  if(weight != -1)
    return nextHop;//TODO:Check the pointer value delivery
  else
    return NULL;*/
  map<int, map<int, record> >::const_iterator thisTable = table.dv.find(number);
  map<int, record>::const_iterator nextHop = thisTable->second.find(destination->number);
  if(nextHop != thisTable->second.end())
    return new Node(nextHop->second.nextHop, 0, 0, 0);
  else
    return NULL;
}

Table *Node::GetRoutingTable() const
{
  return new Table(table);
}


ostream & Node::Print(ostream &os) const
{
  os << "Node(number="<<number<<", lat="<<lat<<", bw="<<bw;
  return os;
}
#endif
