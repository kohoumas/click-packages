#ifndef DATAQUEUESMULTICAST_HH
#define DATAQUEUESMULTICAST_HH

#include <click/element.hh>
#include <click/etheraddress.hh>
#include <click/ipaddress.hh>
#include <click/packet.hh>
#include <click/hashmap.hh>
#include <click/standard/storage.hh>
#include <click/timer.hh>

#include "bpdata.hh"
#include "bpflow.hh"
#include "dataqueues.hh"

#include <click/config.h>

CLICK_DECLS

class DataQueuesMulticast : public DataQueues {
public:

  DataQueuesMulticast();
  ~DataQueuesMulticast();

  const char* class_name() const {return "DataQueuesMulticast";}
  const char* port_count() const {return "-/1-";}
  const char* processing() const {return "h/lh";}
  void *cast(const char *);
  int configure(Vector<String> &, ErrorHandler *);

  void push(int port, Packet *p);  

  static String read_handler_multicast(Element*, void*);
  void add_handlers();

private:

  Weights_Table _weights;
  enum Policy _policy;
  double _epsilon; // used on MMU
  double _vmax; // used on MMU
  double _zeta; // used on MMU
  double _theta; // used on MMU
  double _a; // For utility function g(v)

  DataQueue* next_pull_queue();
  DataQueueMulticast* get_related_multicast_queue(Packet *p, bool dest_queue);
  DataQueue* get_related_queue(Packet *p);
  void do_periodically();
};

CLICK_ENDDECLS

#endif
