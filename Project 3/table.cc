#include "table.h"

#if defined(GENERIC)
ostream & Table::Print(ostream &os) const
{
  // WRITE THIS
  os << "Table()";
  return os;
}
#endif

#if defined(LINKSTATE)
Table::Table() {}

Table::Table(int initNum)
{
  num = initNum;
}

bool Table::IsNewLink(const Link *link) {
    for (deque<const Link *>::const_iterator it = linkque.begin(); it != linkque.end(); it++) {
        if ((*it)->GetSrc() == link->GetSrc() && (*it)->GetDest() == link->GetDest() && (*it)->GetLatency() == link->GetLatency())
            return false;
    }
    return true;
}

void Table::AddLink(const Link *link) {
    if (IsNewLink(link)) {
        linkque.push_back(link);
        unsigned src = link->GetSrc(), dest = link->GetDest();
        topology[src][dest] = link->GetLatency();
        nodeset.insert(src);
        nodeset.insert(dest);
    }
}

#endif

#if defined(DISTANCEVECTOR)

Table::Table() {}

Table::Table(int initNum)
{
  num = initNum;
}

int Table::IsNewLink(const Link *link) {
  cerr << "IsNewLink called" << endl;
  int src = link->GetSrc();
  int dest = link->GetDest();
  if (src != num) {
      cerr << "src != num" << endl;
      return 2;
  }
  if((dv.find(num)->second).find(dest) == (dv.find(num)->second).end()) {
    cerr << "IsNewLink return 1: Link not found" << endl;
    return 1;
  }
  else if((dv.find(num)->second)[dest].latency != link->GetLatency()) {
    cerr << "IsNewLink return 1: Link changed" << endl;
    return 1;
  }
  else {
    cerr << "IsNewLink return 0" << endl;
    return 0;
  }
}

#endif
