#ifndef _table
#define _table


#include <iostream>

using namespace std;

#if defined(GENERIC)
class Table {
  // Students should write this class

 public:
  ostream & Print(ostream &os) const;
};
#endif


#if defined(LINKSTATE)

#include <map>
#include <set>
#include <deque>
#include "link.h"

struct record {
    record() {}
    record(unsigned int n, double l) {
        nextHop = n;
        latency = l;
    }
    unsigned int nextHop;
    double latency;
};

class Table {
  // Students should write this class
  
 public:
  int num;
  deque<const Link *> linkque;
  map<int, record> routes;
  set<int> nodeset;
  map<int, map<int, double> > topology;

  Table();
  Table(int initNum);
  bool IsNewLink(const Link *);
  void AddLink(const Link *link);
  
  ostream & Print(ostream &os) const;
};
#endif

#if defined(DISTANCEVECTOR)

#include <map>
#include "link.h"

struct record {
    record() {}
    record(unsigned int n, double l) {
        nextHop = n;
        latency = l;
    }
    unsigned int nextHop;
    double latency;
};

class Table {
 public:
  int num;
  map<int, double> neighbor;
  map<int, map<int, record> > dv;

  Table();
  Table(int initNum);
  int IsNewLink(const Link *);
  ostream & Print(ostream &os) const;
};
#endif

inline ostream & operator<<(ostream &os, const Table &t) { return t.Print(os);}

#endif