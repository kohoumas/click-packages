/*
 * dataqueuesmulticast.{cc,hh} -- Backpressure multicast data queues
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

#include "dataqueuesmulticast.hh"

CLICK_DECLS

DataQueuesMulticast::DataQueuesMulticast() {}

DataQueuesMulticast::~DataQueuesMulticast() {}

void* DataQueuesMulticast::cast(const char *n) {

  void *ret = DataQueues::cast(n);
  if (ret) return ret;
  else if (strcmp(n, "DataQueuesMulticast") == 0)
    return (DataQueuesMulticast *)this;
  
  return 0;
}

int DataQueuesMulticast::configure(Vector<String> &conf, ErrorHandler *errh) {

  if (_routing == NONSET) _routing = MULTICAST;

  String rates = "no_rates";
  String weights = "no_weights";
  _policy = MMT;
  String policy = "MMT";
  _epsilon = _vmax = _zeta = _theta = _a = 0;
  int res = cp_va_kparse_remove_keywords(conf, this, errh,
			"RATES", 0, cpString, &rates,
			"WEIGHTS", 0, cpString, &weights,
			"POLICY", 0, cpString, &policy,
                        "epsilon", 0, cpDouble, &_epsilon,
                        "vmax", 0, cpDouble, &_vmax,
                        "zeta", 0, cpDouble, &_zeta,
                        "theta", 0, cpDouble, &_theta,
                        "a", 0, cpDouble, &_a,
			cpEnd);

  if (res < 0) return res;

  if (rates.compare("no_rates")) {
    int index = 0, index_nxt;
    do {
      index_nxt = rates.find_left(",,", index);
      String flow = index_nxt != -1 ? rates.substring(index, index_nxt-index) : rates.substring(index);
      index = index_nxt + 2;

      int sub_index = 0, sub_index_nxt;
      sub_index_nxt = flow.find_left(",", sub_index);
      String ip = sub_index_nxt != -1 ? flow.substring(sub_index, sub_index_nxt-sub_index) : flow.substring(sub_index);
      while(sub_index_nxt != -1) {
        sub_index = sub_index_nxt + 1;
        sub_index_nxt = flow.find_left(",", sub_index);
        String mac = sub_index_nxt != -1 ? flow.substring(sub_index, sub_index_nxt-sub_index) : flow.substring(sub_index);
        sub_index = sub_index_nxt + 1;
        sub_index_nxt = flow.find_left(",", sub_index);
        String rate = sub_index_nxt != -1 ? flow.substring(sub_index, sub_index_nxt-sub_index) : flow.substring(sub_index);

        _rates.insert(ip + " + " + mac, atoi(rate.c_str()));
      }
    } while(index_nxt != -1);
  }

  if (weights.compare("no_weights")) {
    int index = 0, index_nxt;
    do {
      index_nxt = weights.find_left(",,", index);
      String flow = index_nxt != -1 ? weights.substring(index, index_nxt-index) : weights.substring(index);
      index = index_nxt + 2;

      int sub_index = 0, sub_index_nxt;
      sub_index_nxt = flow.find_left(",", sub_index);
      String ip = sub_index_nxt != -1 ? flow.substring(sub_index, sub_index_nxt-sub_index) : flow.substring(sub_index);
      while(sub_index_nxt != -1) {
        sub_index = sub_index_nxt + 1;
        sub_index_nxt = flow.find_left(",", sub_index);
        String mac = sub_index_nxt != -1 ? flow.substring(sub_index, sub_index_nxt-sub_index) : flow.substring(sub_index);
        sub_index = sub_index_nxt + 1;
        sub_index_nxt = flow.find_left(",", sub_index);
        String weight = sub_index_nxt != -1 ? flow.substring(sub_index, sub_index_nxt-sub_index) : flow.substring(sub_index);

        _weights.insert(ip + " + " + mac, atoi(weight.c_str()));
      }
    } while(index_nxt != -1);
  }

  /* click_chatter("id: %s, ", _ip.unparse().c_str());
  for (Weights_Table::iterator iter = _weights.begin(); iter.live(); iter++) {
    click_chatter("weight: %s %d", iter.key().c_str(), iter.value());
  } */
  
  if (!policy.compare("MMT"))
    _policy = MMT;
  else if (!policy.compare("MMU"))
    _policy = MMU;
  else if (!policy.compare("PLAIN"))
    _policy = PLAIN;
  else return errh->error("POLICY should be MMT, MMU or PLAIN");

  if (_a < 0) _a = 0; // MMU (specific policy)

  res = DataQueues::configure(conf, errh);
  
  return res; 
}

DataQueue* DataQueuesMulticast::next_pull_queue() {

  DataQueue *max_q = NULL;
  int64_t max_diff = -1; // accepts only non-negative differences

  for (Flow_Queue_Table::iterator fq_iter = get_iterator(); fq_iter.live(); fq_iter++) {
    Flow f = fq_iter.key();
    DataQueueMulticast *qm = (DataQueueMulticast *)fq_iter.value();
    for (Ether_Queue_Table::iterator eq_iter = qm->get_iterator(); eq_iter.live(); eq_iter++) {
      EtherAddress neig = eq_iter.key();
      DataQueue *q = eq_iter.value();
      if (_policy == PLAIN) return q;

      int8_t rate = q->size() ? WIFI_EXTRA_ANNO(q->front())->rate : 1 ;
      if (!rate) rate = 1;
      uint32_t *wp = _weights.findp(q->key());
      assert(wp);
      int64_t my_backlog = (*wp) * q->size();
      HostBPInfo *neig_info = _bpdata->get_info(neig.unparse_colon(), f);
      int64_t neig_backlog = neig_info ? (int32_t)(neig_info->_backlog) : 0 ;

      int64_t diff = (my_backlog - neig_backlog) * rate;
      //click_chatter("id: %s, key: %s, my_backlog: %d, neig_backlog: %d, rate: %d, diff: %d", _ip.unparse().c_str(), q->key().c_str(), my_backlog, neig_backlog, rate, diff);
      if ((diff > max_diff) || (diff == max_diff && rand()%2)) {
        max_q = q;
        max_diff = diff;
      }
    }
  }
  //if(max_q) click_chatter("id: %s, max key: %s, diff: %d", _ip.unparse().c_str(), max_q->key().c_str(), max_diff);
  _metric = max_diff;

  /* 
   * Distributed Scheduling (MUST NOT BE ENABLED in nodes with both ethernet and wireless interfaces)
   *
  for (Neig_Metric_Table::iterator nm_iter = _bpdata->get_metric_iterator(); nm_iter.live(); nm_iter++)
    if (nm_iter.value() > _metric) 
      max_q = NULL;
  */

  return max_q;
}

void DataQueuesMulticast::do_periodically() {

  //_pull_q = NULL;

  DataQueue *max_q = next_pull_queue();

  if (_policy != PLAIN) {
    for (Flow_Queue_Table::iterator fq_iter = get_iterator(); fq_iter.live(); fq_iter++) {
      DataQueueMulticast *qm = (DataQueueMulticast *)fq_iter.value();
#ifdef CLICK_OML
      uint32_t dropped = 0;
#endif
      for (Ether_Queue_Table::iterator eq_iter = qm->get_iterator(); eq_iter.live(); eq_iter++) {
        DataQueue *q = eq_iter.value();
        uint32_t q_size = q->size();
        uint32_t q_drops = q->drops();

        uint32_t added_drops = 0;
        Packet *dropped_packet;
        if (q_size > q_drops)
          added_drops = q_size < _dmax ? q_size : _dmax;
        q->add_drops(added_drops);
#ifdef CLICK_OML
        dropped += added_drops;
#endif
        while (added_drops-- && (dropped_packet = q->pop_front())) dropped_packet->kill();

        uint32_t removed_drops = 0;
        if (q_drops > (_policy == MMT ? _V : _V * _theta))
          removed_drops = q_drops < _dmax ? q_drops : _dmax;
        q->remove_drops(removed_drops);
      }
      if (_policy == MMU) qm->update_q_virtual(); // MMU
#ifdef CLICK_OML
      if (!qm->key().compare("5.0.0.100")) {
        _length100 = (int32_t)qm->size();
        _dropped100 += dropped;
        _droppedlength100 = (int32_t)qm->drops();
      }
      else if (!qm->key().compare("5.0.0.101")) {
        _length101 = (int32_t)qm->size();
        _dropped101 += dropped;
        _droppedlength101 = (int32_t)qm->drops();
      }
#endif
    }
  }

  _guard_packet = max_q && max_q->size() ? max_q->back() : NULL;
  if (!_guard_packet) _lock_pull = true;
  else _lock_pull = false;
  _pull_q = max_q;
}

DataQueueMulticast* DataQueuesMulticast::get_related_multicast_queue(Packet *p, bool dest_queue) {

  assert(p->dst_ip_anno());
  Flow f(_policy == PLAIN ? IPAddress() : p->dst_ip_anno());
  DataQueueMulticast **qmp = (DataQueueMulticast**)_FQ.findp(f);
  DataQueueMulticast *qm = qmp ? *qmp : 
    (_policy == MMU ? 
    new DataQueueMulticast(f._dst.s(), dest_queue, _capacity, &_weights, _V, _maclayer, _video, _policy, _epsilon, _vmax, _zeta, _theta, _a) :
    new DataQueueMulticast(f._dst.s(), dest_queue, _capacity, &_weights, _V, _maclayer, _video, _policy) );
  if (!qmp) _FQ.insert(f, qm);
  return qm;
}

DataQueue* DataQueuesMulticast::get_related_queue(Packet *p) {

  DataQueueMulticast *qm = get_related_multicast_queue(p, false);

  EtherAddress nxt;
  if (_policy == PLAIN) {
    nxt = EtherAddress();
  } else if (_maclayer == WIFIQOS || _maclayer == WIFI) {
    click_wifi *wh = (click_wifi *)p->data();
    nxt = EtherAddress(wh->i_addr1);
  } else {
    click_ether *eh = (click_ether *)p->data();
    nxt = EtherAddress(eh->ether_dhost);
  }

  return qm->get_queue(nxt);
}

void DataQueuesMulticast::push(int port, Packet *p) {

  if (port == 1) {
    // input(1) receives packets that will not be stored, but forwarded to the upper layer, through output(1).
    //   It creates a flow queue in the destinations, for creation of the virtual queues Y/Z in case of MMU.
    DataQueueMulticast *qm = get_related_multicast_queue(p, true); 
    if (_policy == MMU) qm->increase_received(); // MMU
  }
  DataQueues::push(port, p); // calls DataQueuesMulticast::get_related_queue(Packet *p)
  return;
}


// Setup handlers
enum {H_LENGTH, H_DROPS, H_HIGHWATER_LENGTH, H_METRIC};

String DataQueuesMulticast::read_handler_multicast(Element *e, void *thunk) {

  DataQueuesMulticast *d = static_cast<DataQueuesMulticast *>(e);
  int which = reinterpret_cast<intptr_t>(thunk);
  String ret = "";

  if (which == H_METRIC)
    return (String(d->get_metric()) + "\n");

  for (Flow_Queue_Table::iterator fq_iter = d->get_iterator(); fq_iter.live(); fq_iter++) {
    Flow f = fq_iter.key();
    DataQueueMulticast *qm = (DataQueueMulticast *)fq_iter.value();
    ret += f._dst.s() + " ";
    if (which == H_LENGTH) ret += String((int32_t)qm->size()) + " ";
    for (Ether_Queue_Table::iterator eq_iter = qm->get_iterator(); eq_iter.live(); eq_iter++) {
      EtherAddress neig = eq_iter.key();
      DataQueue *q = eq_iter.value();
      ret += "(" + neig.unparse_colon() + " ";
      switch (which) {
        case H_LENGTH:
          ret += String(q->size()); break;
        case H_DROPS:
          ret += String(q->drops()); break;
        case H_HIGHWATER_LENGTH:
          ret += String(q->highwater_length()); break;
      } 
      ret += ")";
    }
    ret += "\n";
  }

  return ret;
}

void DataQueuesMulticast::add_handlers() {
  add_read_handler("length", read_handler_multicast, (void *)H_LENGTH);
  add_read_handler("drops", read_handler_multicast, (void *)H_DROPS);
  add_read_handler("highwater_length", read_handler_multicast, (void *)H_HIGHWATER_LENGTH);
  add_read_handler("metric", read_handler_multicast, (void *)H_METRIC);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(DataQueuesMulticast)

