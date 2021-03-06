#ifndef CLICK_SR2QueryForwarder_HH
#define CLICK_SR2QueryForwarder_HH
#include <click/element.hh>
#include <click/glue.hh>
#include <click/timer.hh>
#include <click/ipaddress.hh>
#include <click/etheraddress.hh>
#include <click/vector.hh>
#include <click/hashmap.hh>
#include <click/deque.hh>
#include <elements/wifi/linktable.hh>
#include <elements/ethernet/arptable.hh>
#include <elements/wifi/path.hh>
#include "sr2queryforwarder.hh"
#include <elements/wifi/rxstats.hh>
CLICK_DECLS

/*
=c
SR2QueryForwarder(IP, ETH, ETHERTYPE, SR2QueryForwarder element, LinkTable element, ARPtable element, 
   [METRIC GridGenericMetric], [WARMUP period in seconds])

=s Roofnet

Forwards Route Queries


*/


class SR2QueryForwarder : public Element {
 public:
  
  SR2QueryForwarder();
  ~SR2QueryForwarder();
  
  const char *class_name() const		{ return "SR2QueryForwarder"; }
  const char *port_count() const		{ return PORTS_1_1; }
  const char *processing() const		{ return PUSH; }
  const char *flow_code() const			{ return "#/#"; }
  int initialize(ErrorHandler *);
  int configure(Vector<String> &conf, ErrorHandler *errh);


  /* handler stuff */
  void add_handlers();

  void push(int, Packet *);

  bool update_link(IPAddress from, IPAddress to, 
		   uint32_t seq, uint32_t age,		   
		   uint32_t metric);
  void forward_query_hook();
  IPAddress get_random_neighbor();


  // List of query sequence #s that we've already seen.
  class Seen {
  public:
    IPAddress _src;
    IPAddress _dst;
    u_long _seq;

    int _count;
    Timestamp _when; /* when we saw the first query */
    Timestamp _to_send;
    bool _forwarded;


    Seen(IPAddress src, IPAddress dst, u_long seq, int fwd, int rev) {
      _src = src; 
      _dst = dst; 
      _seq = seq; 
      _count = 0;
      (void) fwd, (void) rev;
    }
    Seen();
  };

  typedef HashMap<IPAddress, bool> IPMap;
  IPMap _neighbors;
  Vector<IPAddress> _neighbors_v;

  Deque<Seen> _seen;

  int MaxSeen;   // Max size of table of already-seen queries.
  int MaxHops;   // Max hop count for queries.
  Timestamp _query_wait;
  u_long _seq;      // Next query sequence number to use.
  IPAddress _ip;    // My IP address.
  EtherAddress _en; // My ethernet address.
  uint16_t _et;     // This protocol's ethertype

  IPAddress _bcast_ip;

  EtherAddress _bcast;

  class LinkTable *_link_table;
  class ARPTable *_arp_table;

  bool _debug;

  void process_query(struct sr2packet *pk);
  void forward_query(Seen *s);
  static void static_forward_query_hook(Timer *, void *e) { 
    ((SR2QueryForwarder *) e)->forward_query_hook(); 
  }
};


CLICK_ENDDECLS
#endif
