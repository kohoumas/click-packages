#ifndef RECEIVER2OML_HH
#define RECEIVER2OML_HH

#include <click/element.hh>
#include <click/timer.hh>

#include <click/config.h>
#ifdef CLICK_OML
#include "/usr/include/oml2/omlc.h"
#endif

CLICK_DECLS

class Receiver2OML : public Element {
public:
  
  Receiver2OML();
  ~Receiver2OML();

  const char* class_name() const { return "Receiver2OML"; }
  const char* port_count() const { return "1/1"; }
  const char* processing() const { return PUSH; }
  int configure(Vector<String> &, ErrorHandler *);
  int initialize(ErrorHandler *);

  Packet *simple_action(Packet *);

private:

  IPAddress _ip;
  bool _video;

  uint32_t _packets;
  uint64_t _bytes;

  unsigned int _period; // msecs
  Timer _timer;
  Timestamp _next;

  void run_timer(Timer *);

#ifdef CLICK_OML
  int mp_samples_counter;
  OmlMP* mp;
  static OmlMPDef mp_receiver2oml[];
#endif
};

CLICK_ENDDECLS

#endif
