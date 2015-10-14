/*
 * dataqueues.{cc,hh} -- Backpressure data queues
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

#include <click/packet_anno.hh>
#include <clicknet/udp.h>
#include <clicknet/ip.h>

#include <typeinfo>

#include "dataqueues.hh"

CLICK_DECLS

#ifdef CLICK_OML
OmlMPDef DataQueues::mp_dataqueues[] = {
  { "period", OML_UINT32_VALUE },
  { "ip", OML_UINT32_VALUE },
  { "throughput", OML_UINT32_VALUE }, // measured in packets per period
  { "sent100", OML_UINT32_VALUE }, // measured in packets per period
  { "sent101", OML_UINT32_VALUE }, // measured in packets per period
  { "failed", OML_UINT32_VALUE }, // measured in packets per period
  { "length100", OML_INT32_VALUE }, // the average length in a period
  { "length101", OML_INT32_VALUE }, // the average length in a period
  { "dropped100", OML_UINT32_VALUE }, // measured in packets per period
  { "dropped101", OML_UINT32_VALUE }, // measured in packets per period 
  { "droppedlength100", OML_UINT32_VALUE }, // the average dropped queue length in a period
  { "droppedlength101", OML_UINT32_VALUE }, // the average dropped queue length in a period
  { "metric100", OML_UINT32_VALUE },
  { "metric101", OML_UINT32_VALUE },
  { "delay_sec", OML_UINT32_VALUE },
  { "delay_nsec", OML_UINT32_VALUE },
  { "cpu_perc", OML_DOUBLE_VALUE },
  { NULL, (OmlValueT)0 }
};
#endif

DataQueues::DataQueues() : _timer(this), _routing(NONSET) {}

DataQueues::~DataQueues() {
#ifdef CLICK_OML
  omlc_close();
#ifdef HAVE_LIBSIGAR_SIGAR_H | HAVE_SIGAR_H
  sigar_close(_sigar);
#endif
#endif
}

void* DataQueues::cast(const char *n) {

  if (strcmp(n, "Storage") == 0)
    return (Storage *)this;
  else if (strcmp(n, "DataQueues") == 0)
    return (DataQueues *)this;
  else if (strcmp(n, Notifier::EMPTY_NOTIFIER) == 0)
    return static_cast<Notifier *>(&_empty_note);
  
  return 0;
}

int DataQueues::configure(Vector<String> &conf, ErrorHandler *errh) {

  if (_routing == NONSET) _routing = UNICAST;

  _ip = IPAddress();
  _eth = EtherAddress();
  _bpdata = NULL;
  _link_table = NULL;
  _arp_table = NULL;
  _capacity = _period = 0;
  _video = false;
  _maclayer = WIFIQOS;
  String maclayer = "wifiqos";
  _default_rate = 0;
  _rate_active = false;
  _enhanced = true;
  _dmax = _V = _backlog_threshold = 0;
#ifdef CLICK_OML
  mp_period = 3000;
#endif
  int res = cp_va_kparse(conf, this, errh,
			"IP", 0, cpIPAddress, &_ip,
			"ETH", 0, cpEtherAddress, &_eth,
			"BPDATA", 0, cpElement, &_bpdata,
			"LT", 0, cpElement, &_link_table,
			"ARP", 0, cpElement, &_arp_table,
			"CAPACITY", 0, cpUnsigned, &_capacity,
			"PERIOD", 0, cpUnsigned, &_period,
			"VIDEO", 0, cpBool, &_video,
			"MACLAYER", 0, cpString, &maclayer,
                        "BWSHAPER", 0, cpUnsigned, &_default_rate,
			"ENHANCED", 0, cpBool, &_enhanced,
			"dmax", 0, cpUnsigned, &_dmax,
                        "V", 0, cpUnsigned, &_V,
                        "BLThr", 0, cpUnsigned, &_backlog_threshold,
#ifdef CLICK_OML
                        "MPPERIOD", 0, cpUnsigned, &mp_period,
#endif
			cpEnd);

  if (res < 0) return res;

  if (!_ip) 
    return errh->error("IP not specified");
  if (!_eth) 
    return errh->error("ETH not specified");
  if (!_bpdata)
    return errh->error("BPDATA not specified");
  if (_routing == UNICAST && !_link_table) 
    return errh->error("LT not specified");
  if (_routing == UNICAST && !_arp_table) 
    return errh->error("ARP not specified");
  if (!_capacity) 
    return errh->error("CAPACITY not specified");
  if (!_period) 
    return errh->error("PERIOD not specified");

  if (_bpdata->cast("BPData") == 0) 
    return errh->error("BPDATA element is not a BPData");
  if (_routing == UNICAST && _link_table->cast("LinkTable") == 0) 
    return errh->error("LT element is not a LinkTable");
  if (_routing == UNICAST && _arp_table->cast("ARPTable") == 0) 
    return errh->error("ARP element is not a ARPTable");
  
  if (!maclayer.compare("wifiqos"))
    _maclayer = WIFIQOS;
  else if (!maclayer.compare("wifi"))
    _maclayer = WIFI;
  else if (!maclayer.compare("ether"))
    _maclayer = ETHER;
  else return errh->error("MACLAYER should be wifiqos, wifi or ether");

  if (_default_rate) {
    _rate_active = true;
    _rate_shapper.set_rate(_default_rate, errh);
  }

  if (!_V) _V = _capacity;

  _metric = -1;

  _pull_q = NULL;
  _guard_packet = NULL;
  _lock_pull = false;

  _sleepiness = 0;
  _empty_note.initialize(Notifier::EMPTY_NOTIFIER, router());

#ifdef CLICK_OML
  mp_samples_counter_init = mp_period / _period ? mp_period / _period : 1;
  mp_period = mp_samples_counter_init * _period;
  mp_samples_counter = mp_samples_counter_init;
  mp = omlc_add_mp ("dataqueues", mp_dataqueues);
  _packets_rx = 0;
  _sent100 = 0;
  _sent101 = 0;
  _packets_tx_fail = 0;
  _length100 = 0;
  _length101 = 0;
  _dropped100 = 0;
  _dropped101 = 0;
  _droppedlength100 = 0;
  _droppedlength101 = 0;
  _metric100 = 0;
  _metric101 = 0;
  _delay_sec = 0;
  _delay_nsec = 0;

#ifdef HAVE_LIBSIGAR_SIGAR_H | HAVE_SIGAR_H
  sigar_open(&_sigar);
  sigar_cpu_get(_sigar, &_cpu);
#endif
#endif

  return res; 
}

int DataQueues::initialize(ErrorHandler *errh) {
  _timer.initialize(this);
  _next = Timestamp::now();
  _timer.schedule_at(_next);
  return 0;
}

IPAddress DataQueues::get_best_neighbor(Flow f, uint32_t *metric) {

  IPAddress dst(f._dst);

  uint32_t my_backlog = get_backlog(f);
  Path shortest = _link_table->best_route(dst, true);
  uint32_t my_distance = _link_table->get_route_metric(shortest);

  if (!my_backlog || !my_distance || !_link_table->valid_route(shortest)) {
    *metric = 0;
    return IPAddress(0);
  }

  uint32_t max_diff_backlog = _capacity + 1; //my_backlog;
  uint32_t max_diff_distance = 999999; //_link_table->get_link_metric(_ip, shortest[1]);
  uint32_t backlog_factor = 1, distance_factor = 1;
  if (!_enhanced) distance_factor = 0;
  else if (max_diff_backlog < max_diff_distance) backlog_factor = max_diff_distance / max_diff_backlog;
  else if (max_diff_backlog > max_diff_distance) distance_factor = max_diff_backlog / max_diff_distance;

  //click_chatter("id: %s, factors backlog: %u, distance: %u", _ip.unparse().c_str(), backlog_factor, distance_factor);

  IPAddress best_neig = IPAddress(0);
  uint32_t max_bp_metric = 0;
  for (Neig_Flow_BPInfo_Table::iterator nfb_iter = _bpdata->get_iterator(); nfb_iter.live(); nfb_iter++) {

    IPAddress neig(nfb_iter.key());
    HostBPInfo *neig_info = nfb_iter.value()->findp(f);

    uint32_t bp_metric = 0, diff_backlog = 0, diff_distance = 0;
    uint32_t ett_metric = _link_table->get_link_metric(_ip, neig);
    
    if (ett_metric) {
      if (neig_info) {
        if ((my_backlog > neig_info->_backlog + _backlog_threshold) && (my_distance > neig_info->_distance)) {
          uint64_t temp;
          temp = (my_backlog - neig_info->_backlog) * backlog_factor;
          diff_backlog = temp >> 32 ? ~0 : (uint32_t)temp;
          temp = (my_distance - neig_info->_distance) * distance_factor;
          diff_distance = temp >> 32 ? ~0 : (uint32_t)temp;
          temp = (diff_backlog + diff_distance) * (999999 / ett_metric);
          bp_metric = temp >> 32 ? ~0 : (uint32_t)temp;
        }
        else bp_metric = 0;
      }
      else if (neig == shortest[1])
        bp_metric = (neig == dst) ? ~0 : 1; 
      

      if (bp_metric > max_bp_metric) {
        max_bp_metric = bp_metric;
        best_neig = neig;
      }

      //click_chatter("id: %s, flow: %s, neig: %s, backlog: %u - %u => %u, distance: %u - %u => %u, metric: %u", _ip.unparse().c_str(), dst.s().c_str(), neig.s().c_str(), my_backlog, neig_info ? neig_info->_backlog : 0, diff_backlog, my_distance, neig_info ? neig_info->_distance : 0, diff_distance, bp_metric);
    }
  }

#ifdef CLICK_OML
  if (dst == IPAddress("5.0.0.100")) {
    _metric100 = max_bp_metric;
  }
  else if (dst == IPAddress("5.0.0.101")) {
    _metric101 = max_bp_metric;
  }
#endif
  *metric = max_bp_metric;
  return best_neig;
}

DataQueue* DataQueues::next_pull_queue(IPAddress *max_neig) {

  DataQueue *max_q = NULL;
  uint32_t max_metric = 0;

  for (Flow_Queue_Table::iterator fq_iter = get_iterator(); fq_iter.live(); fq_iter++) {
    Flow f = fq_iter.key();
    DataQueue *q = (DataQueue *)fq_iter.value();

    uint32_t metric;
    IPAddress neig = get_best_neighbor(f, &metric);

    if ((metric > max_metric) || (metric && metric == max_metric && rand()%2)) {
      max_metric = metric;
      max_q = q;
      *max_neig = neig;
    }
  }
  _metric = max_metric;

  /* 
   * Distributed Scheduling (MUST NOT BE ENABLED in nodes with both ethernet and wireless interfaces)
   *
  for (Neig_Metric_Table::iterator nm_iter = _bpdata->get_metric_iterator(); nm_iter.live(); nm_iter++)
    if (nm_iter.value() > _metric) 
      max_q = NULL;
  */

  return max_q;
}

void DataQueues::do_periodically() { 

  IPAddress max_neig;
  DataQueue *max_q = next_pull_queue(&max_neig);

  //click_chatter("id: %s, flow: %s, neig: %s, metric: %u", _ip.unparse().c_str(), max_q ? max_q->key().c_str() : "null", max_neig.s().c_str(), _metric);

  for (Flow_Queue_Table::iterator fq_iter = get_iterator(); fq_iter.live(); fq_iter++) {
    Flow f = fq_iter.key();
    DataQueue *q = (DataQueue *)fq_iter.value();
    uint32_t q_size = q->size();
    uint32_t q_drops = q->drops();

    uint32_t added_drops = 0;
    Packet *dropped_packet;
    if (q_size > q_drops)
      added_drops = q_size < _dmax ? q_size : _dmax;
    q->add_drops(added_drops);
#ifdef CLICK_OML
    if (!q->key().compare("5.0.0.100")) {
      _length100 = (int32_t)q->size();
      _dropped100 += added_drops;
    }
    else if (!q->key().compare("5.0.0.101")) {
      _length101 = (int32_t)q->size();
      _dropped101 += added_drops;
    }
#endif
    while (added_drops-- && (dropped_packet = q->pop_front())) dropped_packet->kill();

    uint32_t removed_drops = 0;
    if (q_drops > _V)
      removed_drops = q_drops < _dmax ? q_drops : _dmax;
    q->remove_drops(removed_drops);
  }

  _guard_packet = max_q && max_q->size() ? max_q->back() : NULL;
  if (!_guard_packet) _lock_pull = true;
  else _lock_pull = false;
  _pull_neig = IPAddress(0);
  _pull_q = max_q;
  _pull_neig = max_neig;
}

void DataQueues::run_timer(Timer *) { 

  do_periodically();

  if (_rate_active && _pull_q) {
    uint32_t *rp = _rates.findp(_pull_q->key());
    _rate_shapper.set_rate(rp ? *rp : _default_rate, NULL);    
  }

  _next += Timestamp::make_msec(_period);
  _timer.schedule_at(_next);

#ifdef CLICK_OML
  if (mp) {
    if (!(--mp_samples_counter)) {
#ifdef HAVE_LIBSIGAR_SIGAR_H | HAVE_SIGAR_H
      sigar_cpu_t cpu_new;
      sigar_cpu_get(_sigar, &cpu_new);  
      sigar_cpu_perc_calculate(&_cpu, &cpu_new, &_cpu_perc);
#endif

      mp_samples_counter = mp_samples_counter_init;
      OmlValueU values[17];
      omlc_set_uint32 (values[0], mp_period);
      omlc_set_uint32 (values[1], _ip.data()[3]);
      omlc_set_uint32 (values[2], _packets_rx);
      omlc_set_uint32 (values[3], _sent100);
      omlc_set_uint32 (values[4], _sent101);
      omlc_set_uint32 (values[5], _packets_tx_fail);
      omlc_set_int32 (values[6], _length100);
      omlc_set_int32 (values[7], _length101);
      omlc_set_uint32 (values[8], _dropped100);
      omlc_set_uint32 (values[9], _dropped101);
      omlc_set_uint32 (values[10], _droppedlength100);
      omlc_set_uint32 (values[11], _droppedlength101);
      omlc_set_uint32 (values[12], _metric100);
      omlc_set_uint32 (values[13], _metric101);
      omlc_set_uint32 (values[14], _delay_sec);
      omlc_set_uint32 (values[15], _delay_nsec);
#ifdef HAVE_LIBSIGAR_SIGAR_H | HAVE_SIGAR_H
      omlc_set_double (values[16], (int)(_cpu_perc.combined * 10000)/100.0);
#else
      omlc_set_double (values[16], -1);
#endif
      omlc_inject (mp, values);
      _packets_rx = 0;
      _sent100 = 0;
      _sent101 = 0;
      _packets_tx_fail = 0;
      _length100 = 0;
      _length101 = 0;
      _dropped100 = 0;
      _dropped101 = 0;
      _droppedlength100 = 0;
      _droppedlength101 = 0;
      _metric100 = 0;
      _metric101 = 0;
      _delay_sec = 0;
      _delay_nsec = 0;

#ifdef HAVE_LIBSIGAR_SIGAR_H | HAVE_SIGAR_H
      _cpu = cpu_new;
#endif
    }
  }
#endif
}

DataQueue* DataQueues::get_related_queue(Packet *p) {

  assert(p->dst_ip_anno());
  Flow f(p->dst_ip_anno());
  DataQueue **qp = (DataQueue**)_FQ.findp(f);
  DataQueue *q = qp ? *qp : new DataQueue(f._dst.s(), _capacity, _maclayer, 0, _video) ;
  if (!qp) _FQ.insert(f, q);

  return q;
}

void DataQueues::push(int port, Packet *p) {

  if (port == 0) { // Packets to be forwarded pass from here
    DataQueue *q = get_related_queue(p);
    uint32_t queue_size = q->size();  

#ifdef CLICK_OML
    _packets_rx++;
#endif
    if (queue_size != _capacity) {
      q->push_back(p);
      if (++queue_size > q->highwater_length()) 
        q->set_highwater_length(queue_size);
      	_empty_note.wake();
    }
    else {
#ifdef CLICK_OML
      if (!q->key().compare("5.0.0.100")) {
        _dropped100++;
      }
      else if (!q->key().compare("5.0.0.101")) {
        _dropped101++;
      }
#endif
      p->kill();  
      q->add_drop();
    }
  } 
  else if (port == 1) { // Received packets pass from here
#ifdef CLICK_OML
    click_ip *iph = (click_ip *)(p->data());
    click_udp *udph = (click_udp *)(iph + 1);
    _delay_sec = ntohl(*((uint32_t *)(udph + 1) + 3));
    _delay_nsec = ntohl(*((uint32_t *)(udph + 1) + 4));
    _packets_rx++;
#endif
    output(1).push(p);
  }
  else if (port == 2) { // Transmission failed packets pass from here
#ifdef CLICK_OML
    _packets_tx_fail++;
#endif
    output(2).push(p);
  }
}

Packet* DataQueues::pull(int) {

  if ( (!_pull_q || !_pull_q->size()) ||
       (_routing == UNICAST && _pull_neig == IPAddress(0)) ||
       (_lock_pull) ||
       (_rate_active && !_rate_shapper.need_update(Timestamp::now())) ) {
    if (_sleepiness >= SLEEPINESS_TRIGGER) _empty_note.sleep();
    else _sleepiness++;
    return NULL;
  }
  Packet *p = _pull_q->pop_front();
  _sleepiness = 0;
  if (_rate_active && p) _rate_shapper.update_with(p->length());
  if (p == _guard_packet) _lock_pull = true;
#ifdef CLICK_OML
  if (p) {
    if (p->dst_ip_anno() == IPAddress("5.0.0.100")) _sent100++;
    else if (p->dst_ip_anno() == IPAddress("5.0.0.101")) _sent101++;
  }
#endif
  if (_routing == UNICAST && p) {
    WritablePacket *wp = p->uniqueify();
    click_ether *eh = (click_ether *) wp->data();
    srpacket *pk = (srpacket *) (eh+1);
    pk->set_link_node(pk->num_links(), _pull_neig);
    pk->set_checksum();
    EtherAddress edst = _arp_table->lookup(_pull_neig);
    memcpy(eh->ether_dhost, edst.data(), 6);
    p = wp;
  }
  return p;
}


// Setup handlers
enum {H_LENGTH, H_DROPS, H_HIGHWATER_LENGTH, H_FLOW};

String DataQueues::read_handler(Element *e, void *thunk) {

  DataQueues *d = static_cast<DataQueues *>(e);
  int which = reinterpret_cast<intptr_t>(thunk);
  String ret = "";

  for (Flow_Queue_Table::iterator fq_iter = d->get_iterator(); fq_iter.live(); fq_iter++) {
    Flow f = fq_iter.key();
    DataQueue *q = (DataQueue *)fq_iter.value();

    ret += f._dst.s() + " ";
    switch (which) {
      case H_LENGTH:
        if (!d->_video) ret += String(q->size());
        else ret += String(q->size()) + " (Q:" + String(q->sizeQ()) + ",QI:" + String(q->sizeQI()) + ",QP:" + String(q->sizeQP()) + ")"; 
        break;
      case H_DROPS:
        ret += String(q->drops()); break;
      case H_HIGHWATER_LENGTH:
        ret += String(q->highwater_length()); break;
    }
    ret += "\n";
  }
  return ret;
}

int DataQueues::write_handler(const String &in_s, Element *e, void *vparam, ErrorHandler *errh) {

  DataQueues *f = (DataQueues *)e;
  String s = cp_uncomment(in_s);

  switch((intptr_t)vparam) {
    case H_FLOW:
      f->add_queue(Flow(IPAddress(s)));
      break;
  }
  return 0;
}

void DataQueues::add_handlers() {
  add_read_handler("length", read_handler, (void *)H_LENGTH);
  add_read_handler("drops", read_handler, (void *)H_DROPS);
  add_read_handler("highwater_length", read_handler, (void *)H_HIGHWATER_LENGTH);

  add_write_handler("add_flow", write_handler, (void *)H_FLOW);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(DataQueues)
#ifdef CLICK_OML
#ifdef HAVE_LIBSIGAR_SIGAR_H | HAVE_SIGAR_H
ELEMENT_LIBS(-loml2 -lsigar)
#else
ELEMENT_LIBS(-loml2)
#endif
#endif
ELEMENT_REQUIRES(userlevel int64)

