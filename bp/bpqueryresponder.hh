#ifndef CLICK_BPQUERYRESPONDER_HH
#define CLICK_BPQUERYRESPONDER_HH
#include <click/element.hh>
#include <click/glue.hh>
#include <click/timer.hh>
#include <click/ipaddress.hh>
#include <click/etheraddress.hh>
#include <click/vector.hh>
#include <click/hashmap.hh>
#if CLICK_VERSION_CODE >= CLICK_MAKE_VERSION_CODE(2,0,1)
#include <click/deque.hh>
#define QUEUE_CLASS_NAME Deque
#else
#include <click/dequeue.hh>
#define QUEUE_CLASS_NAME DEQueue
#endif
#include <elements/wifi/linktable.hh>
#include <elements/ethernet/arptable.hh>
#include <elements/wifi/path.hh>
#include <elements/wifi/rxstats.hh>
#include "dataqueues.hh"
CLICK_DECLS

class BPQueryResponder : public Element {
 public:
  
  BPQueryResponder();
  ~BPQueryResponder();
  
  const char *class_name() const		{ return "BPQueryResponder"; }
  const char *port_count() const		{ return PORTS_1_1; }
  const char *processing() const		{ return PUSH; }
  const char *flow_code() const			{ return "#/#"; }
  int initialize(ErrorHandler *);
  int configure(Vector<String> &conf, ErrorHandler *errh);


  /* handler stuff */
  void add_handlers();

  void push(int, Packet *);

  bool update_link(IPAddress from, IPAddress to, uint32_t seq, int metric);

  IPAddress _ip;    // My IP address.
  EtherAddress _en; // My ethernet address.
  uint32_t _et;     // This protocol's ethertype



  class Seen {
  public:
    IPAddress _src;
    IPAddress _dst;
    uint32_t _seq;

    Path last_path_response;
    Seen(IPAddress src, IPAddress dst, uint32_t seq) {
      _src = src;
      _dst = dst;
      _seq = seq;
    }
    Seen();
  };

  QUEUE_CLASS_NAME<Seen> _seen;

  class LinkTable *_link_table;
  class ARPTable *_arp_table;
  class DataQueues *_dataqueues;

  bool _debug;



  int find_dst(IPAddress ip, bool create);
  EtherAddress find_arp(IPAddress ip);
  void got_arp(IPAddress ip, EtherAddress en);

  void start_reply(struct srpacket *pk);
  void forward_reply(struct srpacket *pk);
  void got_reply(struct srpacket *pk);

  void send(WritablePacket *);
};


CLICK_ENDDECLS
#endif
