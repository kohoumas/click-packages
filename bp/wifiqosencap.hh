#ifndef CLICK_WIFIQOSENCAP_HH
#define CLICK_WIFIQOSENCAP_HH
#include <click/element.hh>
#include <clicknet/ether.h>
#include <click/etheraddress.hh>
CLICK_DECLS


class WifiQoSEncap : public Element { public:

  WifiQoSEncap();
  ~WifiQoSEncap();

  const char *class_name() const	{ return "WifiQoSEncap"; }
  const char *port_count() const	{ return PORTS_1_1; }
  const char *processing() const	{ return AGNOSTIC; }

  int configure(Vector<String> &, ErrorHandler *);
  bool can_live_reconfigure() const	{ return true; }

  Packet *simple_action(Packet *);


  void add_handlers();


  bool _debug;

  unsigned _mode;
  EtherAddress _bssid;
  uint16_t _tid;
  class WirelessInfo *_winfo;
 private:

};

CLICK_ENDDECLS
#endif
