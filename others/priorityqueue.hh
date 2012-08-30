#ifndef PRIORITYQUEUE_HH
#define PRIORITYQUEUE_HH

#if CLICK_VERSION_CODE >= CLICK_MAKE_VERSION_CODE(2,0,1)
#include <click/deque.hh>
#define QUEUE_CLASS_NAME Deque
#else
#include <click/dequeue.hh>
#define QUEUE_CLASS_NAME DEQueue
#endif

#include <click/element.hh>
#include <click/etheraddress.hh>
#include <click/hashmap.hh>
#include <click/standard/storage.hh>
#include <click/timer.hh>

#include <click/config.h>
#ifdef CLICK_OML
#include "/usr/include/oml2/omlc.h"
#endif

CLICK_DECLS

class PriorityQueue : public Element, public Storage {
public:

  PriorityQueue();
  ~PriorityQueue();

  const char* class_name() const {return "PriorityQueue";}
  const char* port_count() const {return "-/1-";}
  const char* processing() const {return PUSH_TO_PULL;}
  void *cast(const char *);
  int configure(Vector<String> &, ErrorHandler *);
  int initialize(ErrorHandler *);
  
  void push(int port, Packet *p);
  Packet* pull(int);

  static String read_handler(Element*, void*);
  virtual void add_handlers();

protected:

  EtherAddress _eth; 

  QUEUE_CLASS_NAME<Packet*> *_Q_table;
  QUEUE_CLASS_NAME<Packet*> *_pull_q;
  uint32_t _queues_number;
  uint32_t _queues_proportion;
  uint32_t _capacity_short;
  uint32_t _capacity_long;

  uint32_t _drops;
  uint32_t *_highwater;

  uint64_t _paint_bytes[255]; //bytes transmitted
  uint32_t _iterations;
  uint8_t _paint_goal[255];  // percentage
  bool _paint_achieved[255];
  uint32_t _paint_packets_bucket[255]; //packets in bucket

  uint64_t _paint_samples_bytes[255]; //for the implementation of the 'sampled' policy
  uint32_t _samples_skipped;
  uint32_t _samples_counter;

  unsigned int _period; // msecs
  Timer _timer;
  Timestamp _next;

  void run_timer(Timer *);

#ifdef CLICK_OML
  OmlMP* mp;
  static OmlMPDef mp_def[];
#endif
};

CLICK_ENDDECLS

#endif
