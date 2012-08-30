#ifndef CLICK_BPQUERIER_HH
#define CLICK_BPQUERIER_HH
#include <click/element.hh>
#include <click/glue.hh>
#include <click/timer.hh>
#include <click/ipaddress.hh>
#include <click/etheraddress.hh>
#include <click/vector.hh>
#include <click/hashmap.hh>
#include <elements/wifi/linktable.hh>
#include <elements/wifi/path.hh>
#include "bpforwarder.hh"
#include "dataqueues.hh" // Backpressure
CLICK_DECLS

class BPQuerier : public Element {
 public:
  
  BPQuerier();
  ~BPQuerier();
  
  const char *class_name() const		{ return "BPQuerier"; }
  const char *port_count() const		{ return "2/2"; } // Backpressure (before it was "1/2")
  const char *processing() const		{ return PUSH; }
  const char *flow_code() const			{ return "#/#"; }
  int initialize(ErrorHandler *);
  int configure(Vector<String> &conf, ErrorHandler *errh);


  /* handler stuff */
  void add_handlers();

  void push(int, Packet *);
  void send_query(IPAddress);
  class DstInfo {
  public:
    DstInfo() {memset(this, 0, sizeof(*this)); }
    DstInfo(IPAddress ip) {memset(this, 0, sizeof(*this)); _ip = ip;}
    IPAddress _ip;
    int _best_metric;
    int _count;
    Timestamp _last_query;
    Path _p;
    Timestamp _last_switch;    // last time we picked a new best route
    Timestamp _first_selected; // when _p was first selected as best route
    
  };

  
  typedef HashMap<IPAddress, DstInfo> DstTable;
  DstTable _queries;

  Timestamp _query_wait;

  u_long _seq;      // Next query sequence number to use.
  IPAddress _ip;    // My IP address.
  EtherAddress _en; // My ethernet address.
  uint32_t _et;     // This protocol's ethertype

  IPAddress _bcast_ip;

  EtherAddress _bcast;

  class BPForwarder *_sr_forwarder; // Backpressure
  class LinkTable *_link_table;
  DataQueues *_dataqueues; // Backpressure

  bool _route_dampening;
  bool _debug;

  int _time_before_switch_sec;

};


CLICK_ENDDECLS
#endif
