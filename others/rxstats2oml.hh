#ifndef CLICK_RXSTATS2OML_HH
#define CLICK_RXSTATS2OML_HH
#include <click/element.hh>
#include <click/etheraddress.hh>
#include <click/bighashmap.hh>
#include <click/glue.hh>

#include <click/config.h>
#ifdef CLICK_OML
#include "/usr/include/oml2/omlc.h"
#endif

CLICK_DECLS

class RXStats2OML : public Element { public:

  RXStats2OML();
  ~RXStats2OML();

  const char *class_name() const		{ return "RXStats2OML"; }
  const char *port_count() const		{ return PORTS_1_1; }
  const char *processing() const		{ return AGNOSTIC; }

  Packet *simple_action(Packet *);

  void add_handlers();

  class DstInfo {
  public:
    EtherAddress _eth;
    int _rate;
    int _noise;
    int _signal;

    int _packets;
    unsigned _sum_signal;
    unsigned _sum_noise;
    Timestamp _last_received;

    DstInfo() {
      memset(this, 0, sizeof(*this));
    }

    DstInfo(EtherAddress eth) {
      memset(this, 0, sizeof(*this));
      _eth = eth;
    }

  };
  typedef HashMap<EtherAddress, DstInfo> NeighborTable;
  typedef NeighborTable::const_iterator NIter;

  NeighborTable _neighbors;
  EtherAddress _bcast;
  int _tau;

#ifdef CLICK_OML
  OmlMP* mp;
  static OmlMPDef mp_def[];
#endif
};

CLICK_ENDDECLS
#endif
