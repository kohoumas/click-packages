#ifndef BPSTAT_HH
#define BPSTAT_HH

#include <click/element.hh>
#include <click/etheraddress.hh>
#include <click/ipaddress.hh>
#include <click/timer.hh>
#include <click/packet.hh>
#include <elements/wifi/linktable.hh>
#include <elements/wifi/availablerates.hh>

#include "bpconfig.h"
#include "bpdata.hh"
#include "dataqueues.hh"

CLICK_DECLS

class BPStat : public Element {
public:
  
  BPStat();
  ~BPStat();

  const char* class_name() const { return "BPStat"; }
  const char* port_count() const { return "1/1"; }
  const char* processing() const { return PUSH; }
  int configure(Vector<String> &, ErrorHandler *);
  int initialize(ErrorHandler *);

  Packet *simple_action(Packet *);

private:

  uint16_t _et; 
  IPAddress _ip;
  EtherAddress _eth;
  BPData *_bpdata;
  DataQueues *_dataqueues;
  LinkTable *_link_table;
  AvailableRates *_rtable; // Used (if given) on the retrieval of the basic transmission rate (2Mbps or 6Mbps)
  enum Layer _layer;
  bool _enhanced; // Enhanced Backpressure

  // The following variables are used for the periodic broadcasting
  unsigned int _period; // msecs
  Timer _timer;
  Timestamp _next;

#ifdef CLICK_OML
  OmlMP* mp;
  static OmlMPDef mp_bpstat[];
  sigar_t *_sigar;
  sigar_cpu_t _cpu;
  sigar_cpu_perc_t _cpu_perc;
#endif

  void run_timer(Timer *);
  void send_probe();
};

//  ==== probe format =====
//   click_ether
//   -- 4 optional bytes (IP of this node)
//   -- 2 bytes (number of flows)
//   -- 12 or 8 bytes (flow dst & backlog & distance or flow dst & backlog)
//   -- ...
//  =======================

class FlowBatch {
public:

  Flow _f;
  uint32_t _backlog;
  uint32_t _distance;

  FlowBatch() {}
  FlowBatch(Flow f, uint32_t backlog, uint32_t distance) : _f(f), _backlog(backlog), _distance(distance) {}
};

CLICK_ENDDECLS

#endif
