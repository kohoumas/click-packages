/*
 * receiver2oml.{cc,hh} -- Element for sending OML RX measurements
 * Kostas Choumas
 *
 * Copyright (c) 2012, University of Thessaly
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include <click/confparse.hh>
#include <click/error.hh>

#include <clicknet/ip.h>
#include <clicknet/udp.h>

#include "receiver2oml.hh"

CLICK_DECLS

#ifdef CLICK_OML
OmlMPDef Receiver2OML::mp_receiver2oml[] = {
  { "period", OML_UINT32_VALUE },
  { "ip", OML_UINT32_VALUE },
  { "packets", OML_UINT32_VALUE },
  { "bytes", OML_UINT64_VALUE },
  { NULL, (OmlValueT)0 }
};
#endif

Receiver2OML::Receiver2OML() : _timer(this) {}

Receiver2OML::~Receiver2OML() {}

int Receiver2OML::configure(Vector<String> &conf, ErrorHandler *errh) {

  _video = false;
  int res = cp_va_kparse(conf, this, errh,
			"IP", 0, cpIPAddress, &_ip,
			"PERIOD", 0, cpUnsigned, &_period,
			"VIDEO", 0, cpBool, &_video,
			cpEnd);

  if (res < 0) return res;

  if (!_ip) 
    return errh->error("IP not specified");
  if (!_period) 
    return errh->error("PERIOD not specified");

  _packets = _bytes = 0;

#ifdef CLICK_OML
  mp = omlc_add_mp ("Receiver2OML", mp_receiver2oml);
#endif

  return res;
}

int Receiver2OML::initialize(ErrorHandler *errh) {

  _timer.initialize(this);
  _next = Timestamp::now();
  _timer.schedule_at(_next);
  return 0;
}

void Receiver2OML::run_timer(Timer *) {

#ifdef CLICK_OML
  if (mp) {
    OmlValueU values[4];
    omlc_set_uint32 (values[0], _period);
    omlc_set_uint32 (values[1], _ip.data()[3]);
    omlc_set_uint32 (values[2], _packets);
    omlc_set_uint64 (values[3], _bytes);
    omlc_inject (mp, values);
  }
#endif

  _packets = _bytes = 0;
  
  _next += Timestamp::make_msec(_period);
  _timer.schedule_at(_next);
}

Packet* Receiver2OML::simple_action(Packet *p) {
#ifdef CLICK_OML
  uint16_t dst_port = 0;
  uint32_t iperf_seq;
  uint8_t nal_type;

  /*dst_port = ntohs(*(uint16_t *)(p->data() + sizeof(click_ip) + 2));

  if (_video) {
    nal_type = *(uint8_t *)(p->data() + sizeof(click_ip) + sizeof(click_udp) + RTP_HEADER_LENGTH);
    nal_type = nal_type & 0x1F; // The NAL type is the 5 last bits of the first byte of the NAL header
    //click_chatter("packet with H264 NAL type %d", nal_type);
  } else {
    iperf_seq = ntohl(*(uint32_t *)(p->data() + sizeof(click_ip) + sizeof(click_udp)));
    //click_chatter("packet with iperf seq num %d", iperf_seq);
  }*/

  _packets++;
  _bytes += (p->length() - sizeof(click_ip) - sizeof(click_udp));
#endif
  return p;
}

EXPORT_ELEMENT(Receiver2OML)
CLICK_ENDDECLS
