/*
 * priorityqueue.{cc,hh} -- Traffic shapping for giving priority to particular flows
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

#include <clicknet/wifi.h>
#include <clicknet/ether.h>
#include <clicknet/ip.h>
#include <clicknet/udp.h>

#include "priorityqueue.hh"

CLICK_DECLS

#ifdef CLICK_OML
OmlMPDef PriorityQueue::mp_def[] = {
  { "period", OML_UINT32_VALUE },
  { "shortqueues", OML_UINT32_VALUE },
  { "bucketqueue", OML_UINT32_VALUE },
  { "bucketratio0", OML_UINT32_VALUE },
  { "bucketratio1", OML_UINT32_VALUE },
  { "bucketratio2", OML_UINT32_VALUE },
  { "throughputtotal", OML_DOUBLE_VALUE },
  { "throughputratio0", OML_DOUBLE_VALUE },
  { "throughputratio1", OML_DOUBLE_VALUE },
  { "throughputratio2", OML_DOUBLE_VALUE },
  { NULL, (OmlValueT)0 }
};
#endif

PriorityQueue::PriorityQueue() : _timer(this) {
#ifdef CLICK_OML
  mp = omlc_add_mp("PriorityQueue", mp_def);
#endif
}

PriorityQueue::~PriorityQueue() {}

void* PriorityQueue::cast(const char *n) {

  if (strcmp(n, "Storage") == 0)
    return (Storage *)this;
  else if (strcmp(n, "PriorityQueue") == 0)
    return (PriorityQueue *)this;
  
  return 0;
}

int PriorityQueue::configure(Vector<String> &conf, ErrorHandler *errh) {

  QUEUE_CLASS_NAME<Packet*> *q;
  String goals = "no_goal";
  _queues_number = 1;
  _queues_proportion = 1;
  _samples_skipped = 0;
  int res = cp_va_kparse(conf, this, errh,
			"CAPACITY", 0, cpUnsigned, &_capacity,
			"PERIOD", 0, cpUnsigned, &_period,
			"NUMBER", 0, cpUnsigned, &_queues_number,
			"PROPORTION", 0, cpUnsigned, &_queues_proportion,
			"GOAL", 0, cpString, &goals,
			"SAMPLES", 0, cpUnsigned, &_samples_skipped,
			cpEnd);

  if (res < 0) return res;

  if (!_capacity) 
    return errh->error("CAPACITY not specified");
  if (!_period) 
    return errh->error("PERIOD not specified");

  _Q_table = new QUEUE_CLASS_NAME<Packet*>[_queues_number];
  _pull_q = _Q_table;

  _capacity_short = _capacity / (_queues_number - 1 + _queues_proportion);
  _capacity_long = _capacity - (_queues_number - 1) * _capacity_short;
  for (q = _Q_table; q < _Q_table + _queues_number - 1; q++) q->reserve(_capacity_short);
  q->reserve(_capacity_long);

  memset(_paint_goal, 0, 255);
  if(goals.compare("no_goal")) {
    int64_t index = 0, index_nxt;
    do {
      index_nxt = goals.find_left(",", index);
      String paint = index_nxt != -1 ? goals.substring(index, index_nxt-index) : goals.substring(index);
      index = index_nxt + 1;
      index_nxt = goals.find_left(",", index);
      String goal = index_nxt != -1 ? goals.substring(index, index_nxt-index) : goals.substring(index);
      index = index_nxt + 1;

      _paint_goal[atoi(paint.c_str())] = atoi(goal.c_str());      
    } while(index_nxt != -1);
  }

  memset(_paint_bytes, 0, 255 << 3);
  memset(_paint_achieved, 1, 255);
  memset(_paint_packets_bucket, 0, 255 << 2);
  _iterations = 0;

  _samples_counter = _samples_skipped;

  _drops = 0;
  _highwater = new uint32_t[_queues_number];
  memset(_highwater, 0, _queues_number << 2);

  return res; 
}

int PriorityQueue::initialize(ErrorHandler *errh) {

  _timer.initialize(this);
  _next = Timestamp::now();
  _timer.schedule_at(_next);
  return 0;
}

void PriorityQueue::run_timer(Timer *) {

  int i;
  uint64_t paint_bytes_sum = 0, paint_samples_bytes_sum = 0;
  double throughput_ratio[255], samples_throughput_ratio[255];

  for (i=0; i<255; i++) {
    throughput_ratio[i] = samples_throughput_ratio[i] = 0;
    paint_bytes_sum += _paint_bytes[i];
    paint_samples_bytes_sum += _paint_samples_bytes[i];
  }
  if (paint_bytes_sum) {
    for (i=0; i<255; i++) {
      throughput_ratio[i] = (_paint_bytes[i] * 100.0 / paint_bytes_sum);
      samples_throughput_ratio[i] = (_paint_samples_bytes[i] * 100.0 / paint_samples_bytes_sum);
      _paint_achieved[i] = ( samples_throughput_ratio[i] >= _paint_goal[i] );
    }
  }

#ifdef CLICK_OML
  QUEUE_CLASS_NAME<Packet*> *q;
  uint32_t shorts_len = 0, long_len = 0;
  uint32_t bucket_ratio0 = 0;
  uint32_t bucket_ratio1 = 0;
  uint32_t bucket_ratio2 = 0;
  _iterations++;

  for (q = _Q_table; q < _Q_table + _queues_number - 1; q++)
    shorts_len += q->size();
  long_len = q->size();
  if (long_len) {
    bucket_ratio0 = _paint_packets_bucket[0] * 100 / long_len;
    bucket_ratio1 = _paint_packets_bucket[1] * 100 / long_len;
    bucket_ratio2 = _paint_packets_bucket[2] * 100 / long_len;
  }
  if (mp) {
    OmlValueU values[10];
    omlc_set_uint32(values[0], _period);
    omlc_set_uint32(values[1], shorts_len);
    omlc_set_uint32(values[2], long_len);
    omlc_set_uint32(values[3], bucket_ratio0);
    omlc_set_uint32(values[4], bucket_ratio1);
    omlc_set_uint32(values[5], bucket_ratio2);
    omlc_set_double(values[6], ((double) paint_bytes_sum) / _iterations);
    omlc_set_double(values[7], throughput_ratio[0]);
    omlc_set_double(values[8], throughput_ratio[1]);
    omlc_set_double(values[9], throughput_ratio[2]);
    omlc_inject(mp, values);
  }
#endif

  _next += Timestamp::make_msec(_period);
  _timer.schedule_at(_next);
}

void PriorityQueue::push(int port, Packet *p) {

  if (port == 1) {
    const click_wifi_extra *ceha = (const click_wifi_extra *) p->all_user_anno();
    if (!(WIFI_EXTRA_TX_FAIL & ceha->flags)) {
      _paint_bytes[PAINT_ANNO(p)] += (p->length() - sizeof(click_ether) - sizeof(click_ip) - sizeof(click_udp));
      if (!_samples_counter) {
        _paint_samples_bytes[PAINT_ANNO(p)] += (p->length() - sizeof(click_ether) - sizeof(click_ip) - sizeof(click_udp));
        _samples_counter = _samples_skipped;
      } else _samples_counter--;
    }
    p->kill();
    return;
  }

  bool pushed = false;
  QUEUE_CLASS_NAME<Packet*> *q = _Q_table;
  uint32_t size;
  if (!_paint_achieved[PAINT_ANNO(p)]) {
    for (; !pushed && q < _Q_table + _queues_number - 1; q++) {
      size = q->size();
      if (size != _capacity_short) {
        q->push_back(p);
        pushed = true;
        if (size == _highwater[q - _Q_table]) 
          _highwater[q - _Q_table]++;
      }
    }
  } else { q += (_queues_number - 1); }
  if (!pushed) {
    size = q->size();
    if (size != _capacity_long) {
      q->push_back(p);
      pushed = true;
      if (size == _highwater[q - _Q_table]) 
        _highwater[q - _Q_table]++;
      _paint_packets_bucket[PAINT_ANNO(p)]++;
    }
  }
  if (!pushed) {
    p->kill();
    _drops++;
  }
  return;
}

Packet* PriorityQueue::pull(int) {

  Packet *p = NULL;
  QUEUE_CLASS_NAME<Packet*> *q;
  for (q = _pull_q; !p && q < _Q_table + _queues_number; q++) {
    if (q->size()) {
      p = q->front();
      q->pop_front();
    }
  }
  if (!p) {
    for (q = _Q_table; !p && q < _pull_q; q++) {
      if (q->size()) {
        p = q->front();
        q->pop_front();
      }
    }
  }
  _pull_q = q;

  if (p) {
    if (q == (_Q_table + _queues_number)) _paint_packets_bucket[PAINT_ANNO(p)]--;
  }
  return p;
}


// Setup handlers
enum {H_LENGTH, H_CAPACITY, H_GOALS, H_DROPS, H_HIGHWATER};

String PriorityQueue::read_handler(Element *e, void *thunk) {

  PriorityQueue *d = static_cast<PriorityQueue *>(e);
  int which = reinterpret_cast<intptr_t>(thunk);
  
  uint32_t total = 0, i = 0;
  QUEUE_CLASS_NAME<Packet*> *q;
  String ret = "";
  switch (which) {
    case H_LENGTH:
      for (q = d->_Q_table; q < d->_Q_table + d->_queues_number; q++) {
        total += q->size();
        ret += String(q->size()) + " "; 
      }
      ret = String(total) + " : " + ret; 
      break;
    case H_CAPACITY:
      ret += String(d->_capacity) + " : (short) " + String(d->_capacity_short) + " (long) " + String(d->_capacity_long);
      break;
    case H_GOALS:
      for (i = 0; i < 255; i++)
        if (d->_paint_goal[i]) 
          ret += String(i) + "->" + (d->_paint_achieved[i] ? String("y") : String("n")) + " ";
      break;
    case H_DROPS:
      ret = String(d->_drops); 
      break;
    case H_HIGHWATER:
      for (i = 0; i < d->_queues_number; i++) {
        total += d->_highwater[i];
        ret += String(d->_highwater[i]) + " "; 
      }
      ret = String(total) + " : " + ret; 
      break;
  }
  ret += "\n";
  return ret;
}

void PriorityQueue::add_handlers() {
  add_read_handler("length", read_handler, (void *)H_LENGTH);
  add_read_handler("capacity", read_handler, (void *)H_CAPACITY);
  add_read_handler("goals", read_handler, (void *)H_GOALS);
  add_read_handler("drops", read_handler, (void *)H_DROPS);
  add_read_handler("highwater_length", read_handler, (void *)H_HIGHWATER);
}


CLICK_ENDDECLS
EXPORT_ELEMENT(PriorityQueue)
#ifdef CLICK_OML
#if defined(HAVE_LIBSIGAR_SIGAR_H) || defined(HAVE_SIGAR_H)
ELEMENT_LIBS(-loml2 -locomm -lsigar)
#else
ELEMENT_LIBS(-loml2 -locomm)
#endif
#endif

