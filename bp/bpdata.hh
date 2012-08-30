#ifndef BPDATA_HH
#define BPDATA_HH

#include <click/element.hh>
#include <click/etheraddress.hh>
#include <click/ipaddress.hh>
#include <click/hashmap.hh>

#include "bpconfig.h"
#include "bpflow.hh"

CLICK_DECLS

class HostBPInfo {
public:

  uint32_t _backlog;
  uint32_t _distance;

  bool _static_distance;
  
  HostBPInfo() {};
  HostBPInfo(uint32_t backlog, uint32_t distance) : _backlog(backlog), _distance(distance), _static_distance(false) {};
};

typedef HashMap<Flow, HostBPInfo> Flow_BPInfo_Table;
typedef HashMap<String, Flow_BPInfo_Table*> Neig_Flow_BPInfo_Table;
typedef HashMap<String, int64_t> Neig_Metric_Table;

class BPData: public Element {
public:

  BPData();
  ~BPData();
  
  const char* class_name() const { return "BPData"; }
  int configure(Vector<String> &conf, ErrorHandler *errh);

  // Mutators
  void set_enhanced(bool enhanced) { _enhanced = enhanced; } 

  // Accessors
  Flow_BPInfo_Table* get_table(String neig);
  HostBPInfo* get_info(String neig, Flow f);
  Neig_Flow_BPInfo_Table::iterator get_iterator() { return _NFB.begin(); }
  int64_t* get_metric(String neig) { return _NM.findp(neig); }
  void set_metric(String neig, int64_t metric) { _NM.insert(neig, metric); }
  Neig_Metric_Table::iterator get_metric_iterator() { return _NM.begin(); }

  static String read_handler(Element *, void *);
  static int write_handler(const String&, Element*, void*, ErrorHandler*);
  void add_handlers();

private:

  Neig_Flow_BPInfo_Table _NFB;
  Neig_Metric_Table _NM;
  bool _enhanced; // Enhanced Backpressure
};

CLICK_ENDDECLS

#endif
