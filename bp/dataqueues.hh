#ifndef DATAQUEUES_HH
#define DATAQUEUES_HH

#include <click/element.hh>
#include <click/etheraddress.hh>
#include <click/ipaddress.hh>
#include <click/packet.hh>
#include <click/hashmap.hh>
#include <click/standard/storage.hh>
#include <elements/wifi/linktable.hh>
#include <elements/ethernet/arptable.hh>
#include <click/timer.hh>
#include <click/gaprate.hh>
#include <click/notifier.hh>

#include "bpdata.hh"
#include "bpflow.hh"
#include "dataqueue.hh"

#include <click/config.h>
#ifdef CLICK_OML
#include <oml2/omlc.h>
#ifdef HAVE_LIBSIGAR_SIGAR_H
#include <libsigar/sigar.h>
extern "C" {
#include <libsigar/sigar_format.h>
}
#endif
#ifdef HAVE_SIGAR_H
#include <sigar.h>
extern "C" {
#include <sigar_format.h>
}
#endif
#endif


CLICK_DECLS

enum Routing {NONSET, UNICAST, MULTICAST}; //Unicast or Multicast

typedef HashMap<Flow, BPQueue*> Flow_Queue_Table;
typedef HashMap<String, uint32_t> Rates_Table;

class DataQueues : public Element, public Storage {
public:

  DataQueues();
  ~DataQueues();

  const char* class_name() const {return "DataQueues";}
  const char* port_count() const {return "-/1-";}
  const char* processing() const {return "h/lh";}
  void *cast(const char *);
  int configure(Vector<String> &, ErrorHandler *);
  int initialize(ErrorHandler *);
  
  void push(int port, Packet *p);
  Packet* pull(int);

  // Mutators
  void add_queue(Flow f) { if (!_FQ.findp(f)) _FQ.insert(f, new DataQueue(f._dst.s(), _capacity, _maclayer, 0, _video)); }

  // Accessors
  Flow_Queue_Table::iterator get_iterator() { return _FQ.begin(); }
  int64_t get_metric() { return _metric; }

  static String read_handler(Element*, void*);
  static int write_handler(const String&, Element*, void*, ErrorHandler*);
  virtual void add_handlers();

protected:

  Flow_Queue_Table _FQ;

  IPAddress _ip;
  EtherAddress _eth; 
  BPData *_bpdata;
  LinkTable *_link_table;
  ARPTable *_arp_table;
  bool _video; // Video awareness
  enum Layer _maclayer; // The MAC Layer of the received packets (Ethernet, WiFi, WiFiQos)
  GapRate _rate_shapper; // Output rate shapper
  uint32_t _default_rate; // Output rate shapper
  Rates_Table _rates; // Output rate shapper
  bool _rate_active; // Output rate shapper
  bool _enhanced; // Enhanced Backpressure
  uint32_t _dmax;
  uint32_t _V;
  uint32_t _backlog_threshold;

  DataQueue *_pull_q;
  IPAddress _pull_neig;
  Packet *_guard_packet;
  bool _lock_pull;
  enum Routing _routing; // multicast or unicast
  int64_t _metric; // Maximum metric of all included backlogs

  // The following variables and function are used for the periodic check for dropping packets
  unsigned int _period; // msecs
  Timer _timer;
  Timestamp _next;

  enum { SLEEPINESS_TRIGGER = 9 };
  int _sleepiness;
  ActiveNotifier _empty_note;

#ifdef CLICK_OML
  unsigned int mp_period; // msecs
  uint32_t mp_samples_counter_init;
  uint32_t mp_samples_counter;
  OmlMP* mp;
  static OmlMPDef mp_dataqueues[];
  uint32_t _packets_rx;
  uint32_t _sent100;
  uint32_t _sent101;
  uint32_t _packets_tx_fail;
  int32_t _length100;
  int32_t _length101;
  uint32_t _dropped100;
  uint32_t _dropped101;
  uint32_t _droppedlength100;
  uint32_t _droppedlength101;
  uint32_t _metric100;
  uint32_t _metric101;
  uint32_t _delay_sec;
  uint32_t _delay_nsec;
#ifdef HAVE_LIBSIGAR_SIGAR_H | HAVE_SIGAR_H
  sigar_t *_sigar;
  sigar_cpu_t _cpu;
  sigar_cpu_perc_t _cpu_perc;
#endif
#endif

  uint32_t get_backlog(Flow f) {
    BPQueue **qp = _FQ.findp(f);
    return qp ? (*qp)->size() : 0;
  }
  IPAddress get_best_neighbor(Flow f, uint32_t *metric);
  virtual DataQueue* next_pull_queue(IPAddress *max_neig);
  virtual DataQueue* get_related_queue(Packet *p);
  virtual void do_periodically();
  void run_timer(Timer *);
};

CLICK_ENDDECLS

#endif
