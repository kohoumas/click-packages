/*
 * bpstat.{cc,hh} -- Backpressure statistics
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

#include <clicknet/ether.h>
#include <clicknet/wifi.h>

#include "bpstat.hh"

CLICK_DECLS

#ifdef CLICK_OML
OmlMPDef BPStat::mp_bpstat[] = {
  { "period", OML_UINT32_VALUE },
  { "ip", OML_UINT32_VALUE },
  { "cpu_perc", OML_DOUBLE_VALUE },
  { NULL, (OmlValueT)0 }
};
#endif

BPStat::BPStat() : _timer(this) {}

BPStat::~BPStat() {
#ifdef CLICK_OML
  omlc_close();
#if defined(HAVE_LIBSIGAR_SIGAR_H) || defined(HAVE_SIGAR_H)
  sigar_close(_sigar);
#endif
#endif
}

int BPStat::configure(Vector<String> &conf, ErrorHandler *errh) {

  _et = 0;
  _ip = IPAddress();
  _eth = EtherAddress();
  _period = 1000;
  _bpdata = NULL;
  _dataqueues = NULL;
  _link_table = NULL;
  _rtable = NULL;
  _layer = IP;
  String layer = "ip";
  _enhanced = true;
  int res = cp_va_kparse(conf, this, errh,
			"ETHTYPE", 0, cpUnsigned, &_et,
			"IP", 0, cpIPAddress, &_ip,
			"ETH", 0, cpEtherAddress, &_eth,
			"PERIOD", 0, cpUnsigned, &_period,
			"BPDATA", 0, cpElement, &_bpdata,
			"DQ", 0, cpElement, &_dataqueues,
			"LT", 0, cpElement, &_link_table,
			"RT", 0, cpElement, &_rtable,
			"LAYER", 0, cpString, &layer,
			"ENHANCED", 0, cpBool, &_enhanced,
			cpEnd);

  if (res < 0) return res;

  if (!_et)
    return errh->error("ETHTYPE not specified");
  if (!_ip)
    return errh->error("IP not specified");
  if (!_eth)
    return errh->error("ETH not specified");
  if (!_bpdata)
    return errh->error("BPDATA not specified");
  if (!_dataqueues)
    return errh->error("DQ not specified");
  if (!_link_table && _enhanced) 
    return errh->error("LT not specified, although element is in Enhanced operation");
  
  if (_bpdata->cast("BPData") == 0) 
    return errh->error("BPDATA element is not a BPData");
  if (_dataqueues->cast("DataQueues") == 0)
    return errh->error("DQ element is not a DataQueues");
  if (_link_table && _link_table->cast("LinkTable") == 0)
    return errh->error("LT element is not a LinkTable");
  if (_rtable && _rtable->cast("AvailableRates") == 0)
    return errh->error("RT element is not a AvailableRates");

  if (!layer.compare("eth"))
    _layer = ETHER;
  else if (!layer.compare("ip"))
    _layer = IP;
  else return errh->error("LAYER should be ETHER or IP");

  _bpdata->set_enhanced(_enhanced);

#ifdef CLICK_OML
  mp = omlc_add_mp ("bpstat", mp_bpstat);
#if defined(HAVE_LIBSIGAR_SIGAR_H) || defined(HAVE_SIGAR_H)
  sigar_open(&_sigar);
  sigar_cpu_get(_sigar, &_cpu);
#endif
#endif

  return res;
}

void add_jitter_bp(unsigned int max_jitter, Timestamp *t) {
    uint32_t j = click_random(0, 2 * max_jitter);
    if (j >= max_jitter) {
	*t += Timestamp::make_msec(j - max_jitter);
    } else {
	*t -= Timestamp::make_msec(j);
    }
    return;
}

int BPStat::initialize(ErrorHandler *errh) {

  if (noutputs() > 0) {
    if (!_eth) return errh->error("Source Ethernet address must be specified to send probes");

    _timer.initialize(this);
    _next = Timestamp::now();

    unsigned max_jitter = _period / 10;
    add_jitter_bp(max_jitter, &_next);

    _timer.schedule_at(_next);
  }

  return 0;
}

void BPStat::run_timer(Timer *) {
  unsigned max_jitter = _period / 10;

  send_probe();

#ifdef CLICK_OML
  if (mp) {
    if (1) {
#if defined(HAVE_LIBSIGAR_SIGAR_H) || defined(HAVE_SIGAR_H)
      sigar_cpu_t cpu_new;
      sigar_cpu_get(_sigar, &cpu_new);  
      sigar_cpu_perc_calculate(&_cpu, &cpu_new, &_cpu_perc);
#endif

      OmlValueU values[3];
      omlc_set_uint32 (values[0], _period);
      omlc_set_uint32 (values[1], _ip.data()[3]);
#if defined(HAVE_LIBSIGAR_SIGAR_H) || defined(HAVE_SIGAR_H)
      omlc_set_double (values[2], (int)(_cpu_perc.combined * 10000)/100.0);
#else
      omlc_set_double (values[2], -1);
#endif
      omlc_inject (mp, values);

#if defined(HAVE_LIBSIGAR_SIGAR_H) || defined(HAVE_SIGAR_H)
      _cpu = cpu_new;
#endif
    }
  }
#endif

  _next += Timestamp::make_msec(_period);
  add_jitter_bp(max_jitter, &_next);
  _timer.schedule_at(_next);
}


void BPStat::send_probe() {

  Vector<FlowBatch> batches;
  for (Flow_Queue_Table::iterator fq_iter = _dataqueues->get_iterator(); fq_iter.live(); fq_iter++) {

    Flow f = fq_iter.key();
    uint32_t backlog = fq_iter.value()->size();
    uint32_t distance = _enhanced ? _link_table->get_host_metric_from_me(f._dst) : 0; // Length measured as sum of ett values.
    //uint32_t distance = _link_table->best_route(f._dst, true).size(); // Length measured as hop count.
    if (_enhanced && !distance) distance = ~0;
    
    FlowBatch batch = FlowBatch(f, backlog, distance);
    batches.push_front(batch);
  }

  int size = sizeof(click_ether);
  if (_layer == IP) size += 4; // 4 bytes for the IP address (if it is required)
  size += 8; // 4 bytes for the maximum metric
  size += 2; // 2 bytes for the number of flow batches
  size += batches.size() * (_enhanced ? 12 : 8); // 12 or 8 bytes for each flow batch


  WritablePacket *p = Packet::make(size);
  if (p == 0) {
    click_chatter("%{element}: cannot make packet!", this);
    return;
  }
  // packet data should be 4 byte aligned
  assert(((uintptr_t)(p->data()) % 4) == 0);

  memset(p->data(), 0, p->length());

  p->set_timestamp_anno(Timestamp::now());

  // fill in ethernet header
  click_ether *eh = (click_ether *) p->data();
  memset(eh->ether_dhost, 0xff, 6); // broadcast
  memcpy(eh->ether_shost, _eth.data(), 6);
  eh->ether_type = htons(_et);

  // fill in the IP address of this node (if _layer is IP)
  uint32_t *ptr32 = (uint32_t *) (eh + 1);
  if (_layer == IP) *ptr32++ = _ip.addr();

  // fill in the minimum metric of this node
  int64_t *ptr64 = (int64_t *) ptr32;
  *ptr64++ = _dataqueues->get_metric();

  // fill in the information of the known flows of this node
  uint16_t *ptr16 = (uint16_t *) ptr64;
  *ptr16++ = batches.size(); //number of flow batches

  ptr32 = (uint32_t *)ptr16;

  for (Vector<FlowBatch>::const_iterator batches_iter = batches.begin(); batches_iter < batches.end(); batches_iter++) {
    const FlowBatch *b = batches_iter;
    *ptr32++ = b->_f._dst.addr();
    *ptr32++ = b->_backlog;
    if(_enhanced) *ptr32++ = b->_distance;
  }

  if (_rtable) {
    click_wifi_extra *ceh = (click_wifi_extra *) p->user_anno();
    ceh->magic = WIFI_EXTRA_MAGIC;
    ceh->rate = _rtable->lookup(_eth)[0];
  }

  checked_output_push(0, p);
}

Packet* BPStat::simple_action(Packet *p) {

  // read the ethernet header
  click_ether *eh = (click_ether *) p->data();
  if (ntohs(eh->ether_type) != _et) {
    click_chatter("%{element}: got non-BPStat packet type", this);
    p->kill();
    return 0;
  }

  // read the IP address of the node that sent this probe (if _layer is IP)
  uint32_t *ptr32 = (uint32_t *) (eh + 1);

  String neig = _layer == IP ? IPAddress(*ptr32++).s() : EtherAddress(eh->ether_shost).unparse_colon();
  Flow_BPInfo_Table *fb = _bpdata->get_table(neig);

  int64_t *ptr64 = (int64_t *) ptr32;
  _bpdata->set_metric(neig, *ptr64++);

  uint16_t *ptr16 = (uint16_t *) ptr64;
  int nof_flow_batches_left = *ptr16++;

  ptr32 = (uint32_t *) ptr16;
  
  while ((nof_flow_batches_left-- >= 1) && (ptr32 < (uint32_t *)(p->data() + p->length()))) {
    uint32_t dst = *ptr32++;
    uint32_t backlog = *ptr32++;
    uint32_t distance = _enhanced ? *ptr32++ : 0;
    HostBPInfo *neig_info = fb->findp(Flow(dst));
    if (neig_info && neig_info->_static_distance)
      fb->insert(Flow(dst), HostBPInfo(backlog, neig_info->_distance));
    else
      fb->insert(Flow(dst), HostBPInfo(backlog, distance)); //update neighbor's info
  }

  p->kill();
  return 0;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(BPStat)
#ifdef CLICK_OML
#if defined(HAVE_LIBSIGAR_SIGAR_H) || defined(HAVE_SIGAR_H)
ELEMENT_LIBS(-loml2 -locomm -lsigar)
#else
ELEMENT_LIBS(-loml2 -locomm)
#endif
#endif

