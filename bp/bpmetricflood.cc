/*
 * bpmetricflood.{cc,hh} -- Backpressure metric flood
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
#include "bpmetricflood.hh"
#include <click/ipaddress.hh>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/straccum.hh>
#include <clicknet/ether.h>
#include <elements/wifi/sr/srpacket.hh>
CLICK_DECLS



BPMetricFlood::BPMetricFlood()
  :  _ip(),
     _en(),
     _et(0),
     _link_table(0),
     _arp_table(0)
{

  MaxSeen = 200;
  MaxHops = 30;

  // Pick a starting sequence number that we have not used before.
  _seq = Timestamp::now().usec();

  _query_wait.assign(5, 0);


  static unsigned char bcast_addr[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
  _bcast = EtherAddress(bcast_addr);
}

BPMetricFlood::~BPMetricFlood()
{
}

int
BPMetricFlood::configure (Vector<String> &conf, ErrorHandler *errh)
{
  int ret;
  _debug = false;
  ret = cp_va_kparse(conf, this, errh,
		     "ETHTYPE", 0, cpUnsigned, &_et,
		     "IP", 0, cpIPAddress, &_ip,
		     "ETH", 0, cpEtherAddress, &_en,
		     "LT", 0, cpElement, &_link_table,
		     /* below not required */
		     "ARP", 0, cpElement, &_arp_table,
		     "DEBUG", 0, cpBool, &_debug,
		     cpEnd);

  if (!_et) 
    return errh->error("ETHTYPE not specified");
  if (!_ip) 
    return errh->error("IP not specified");
  if (!_en) 
    return errh->error("ETH not specified");

  if (!_link_table) 
    return errh->error("LT not specified");


  if (_link_table->cast("LinkTable") == 0) 
    return errh->error("LT element is not a LinkTable");
  if (_arp_table && _arp_table->cast("ARPTable") == 0) 
    return errh->error("ARP element is not a ARPTable");

  return ret;
}

int
BPMetricFlood::initialize (ErrorHandler *)
{
  return 0;
}

IPAddress
BPMetricFlood::get_random_neighbor()
{
  if (!_neighbors_v.size()) {
    return IPAddress();
  }
  int ndx = click_random(0, _neighbors_v.size() - 1);
  return _neighbors_v[ndx];

}


bool
BPMetricFlood::update_link(IPAddress from, IPAddress to, 
			      uint32_t seq, uint32_t age,
			      uint32_t metric) {
  if (!from || !to || !metric) {
    return false;
  }
  if (_link_table && !_link_table->update_link(from, to, seq, age, metric)) {
    click_chatter("%{element} couldn't update link %s > %d > %s\n",
		  this,
		  from.unparse().c_str(),
		  metric,
		  to.unparse().c_str());
    return false;
  }
  return true;
}

void
BPMetricFlood::forward_query_hook() 
{
  Timestamp now = Timestamp::now();
  for (int x = 0; x < _seen.size(); x++) {
    if (_seen[x]._to_send < now && !_seen[x]._forwarded) {
      forward_query(&_seen[x]);
    }
  }
}
void
BPMetricFlood::forward_query(Seen *s)
{

  s->_forwarded = true;
// SRCR needs dijkstra to find shortest path, not Backpressure
  //_link_table->dijkstra(false);

  Packet *p_in = s->_p;
  s->_p = 0;

  if (!p_in) {
    return;
  }

  if (0) {
    StringAccum sa;
    sa << Timestamp::now() - s->_when;
    click_chatter("%{element} :: %s :: waited %s\n",
		  this,
		  __func__,
		  sa.take_string().c_str());
  }

  IPAddress src = s->_src;
// SRCR finds the shortest path
  //Path best = _link_table->best_route(src, false);
// Backpressure uses the path that request followed until now
  Path best = s->_path;
  bool best_valid = _link_table->valid_route(best);

  if (!best_valid) {
	  if (_debug) {
		  click_chatter("%s : %{element} :: %s :: invalid route from src %s\n", _ip.unparse().c_str(), 
				this,
				__func__,
				src.unparse().c_str());
	  }
    p_in->kill();
    return;
  }

  if (_debug) {
     click_chatter("%s : %{element}: forward_query %s -> %s (%s) %d\n", _ip.unparse().c_str(), 
		  this,
		  s->_src.unparse().c_str(),
		  s->_dst.unparse().c_str(),
		  path_to_string(s->_path).c_str(),
		  s->_seq);
  }

  int links = best.size() - 1;

  click_ether *eh = (click_ether *) p_in->data();
  struct srpacket *pk = (struct srpacket *) (eh+1);

  int extra = pk->hlen_wo_data() + sizeof(click_ether);
  p_in->pull(extra);

  int dlen = p_in->length();
  extra = srpacket::len_wo_data(links) + sizeof(click_ether);
  WritablePacket *p = p_in->push(extra);

  if (p == 0)
    return;
  eh = (click_ether *) p->data();
  pk = (struct srpacket *) (eh+1);
  memset(pk, '\0', extra);
  pk->_version = _sr_version;
  pk->_type = PT_DATA;
  pk->_flags = 0;
  pk->_qdst = s->_dst;
  pk->set_seq(s->_seq);
  pk->set_num_links(links);
  pk->set_data_len(dlen);

  for (int i = 0; i < links; i++) {
    pk->set_link(i,
		 best[i], best[i+1],
		 _link_table->get_link_metric(best[i], best[i+1]),
		 _link_table->get_link_metric(best[i+1], best[i]),
		 _link_table->get_link_seq(best[i], best[i+1]),
		 _link_table->get_link_age(best[i], best[i+1]));
  }
	       
  eh->ether_type = htons(_et);
  memcpy(eh->ether_shost, _en.data(), 6);
  memset(eh->ether_dhost, 0xff, 6);
  output(0).push(p);
}


void 
BPMetricFlood::start_flood(Packet *p_in) {
  IPAddress qdst = p_in->dst_ip_anno();
  int dlen = p_in->length();
  unsigned extra = srpacket::len_wo_data(0) + sizeof(click_ether);
  WritablePacket *p = p_in->push(extra);
  if (p == 0)
    return;
  click_ether *eh = (click_ether *) p->data();
  eh->ether_type = htons(_et);
  memcpy(eh->ether_shost, _en.data(), 6);
  memset(eh->ether_dhost, 0xff, 6);

  struct srpacket *pk = (struct srpacket *) (eh+1);
  memset(pk, '\0', srpacket::len_wo_data(0));
  pk->_version = _sr_version;
  pk->_type = PT_DATA;
  pk->_flags = 0;
  pk->_qdst = qdst;
  pk->set_seq(++_seq);
  pk->set_num_links(0);
  pk->set_link_node(0,_ip);
  pk->set_data_len(dlen);


  if (_debug) {
    click_chatter("%s : %{element} start_query %s %d\n", _ip.unparse().c_str(), 
		  this, qdst.unparse().c_str(), _seq);

  }

  output(0).push(p);
}

void 
BPMetricFlood::process_flood(Packet *p_in) {
  click_ether *eh = (click_ether *) p_in->data();
  struct srpacket *pk = (struct srpacket *) (eh+1);
  if(eh->ether_type != htons(_et)) {
    click_chatter("%{element}: bad ether_type %04x",
		  this,
		  ntohs(eh->ether_type));
    p_in->kill();
    return;
  }
  if (EtherAddress(eh->ether_shost) == _en) {
    click_chatter("%{element}: packet from me",
		  this);
    p_in->kill();
    return;
  }

  /* update the metrics from the packet */
  for(int i = 0; i < pk->num_links(); i++) {
    IPAddress a = pk->get_link_node(i);
    IPAddress b = pk->get_link_node(i+1);
    uint32_t fwd_m = pk->get_link_fwd(i);
    uint32_t rev_m = pk->get_link_fwd(i);
    uint32_t seq = pk->get_link_seq(i);
    uint32_t age = pk->get_link_age(i);

    if (fwd_m && !update_link(a, b, seq, age, fwd_m)) {
      click_chatter("%{element} couldn't update fwd_m %s > %d > %s\n",
		    this,
		    a.unparse().c_str(),
		    fwd_m,
		    b.unparse().c_str());
    }
    if (rev_m && !update_link(b, a, seq, age, rev_m)) {
      click_chatter("%{element} couldn't update rev_m %s > %d > %s\n",
		    this,
		    b.unparse().c_str(),
		    rev_m,
		    a.unparse().c_str());
    }
  }
  
  
  IPAddress neighbor = pk->get_link_node(pk->num_links());
  sr_assert(neighbor);
  
  if (!_neighbors[neighbor]) {
    _neighbors[neighbor] = true;
    _neighbors_v.push_back(neighbor);
  }
  
  if (_arp_table) {
    _arp_table->insert(neighbor, EtherAddress(eh->ether_shost));
  }
  
  IPAddress src(pk->get_link_node(0));
  IPAddress dst(pk->_qdst);
  u_long seq = pk->seq();

  int si = 0;
  
// SRCR reject requests with common src and seq
  /*for(si = 0; si < _seen.size(); si++){
    if(src == _seen[si]._src && seq == _seen[si]._seq) {
      _seen[si]._count++;
      p_in->kill();
      return;
    }
  }*/
// Backpressure needs to know all paths, so nodes forward requests with common src and seq 
//    but different paths (if these paths don't have circles)
  Path path(pk->get_path());
  if (index_of(path, _ip) != -1) {
    p_in->kill();
    return;
  }
  path.push_back(_ip);
  
  if (_seen.size() >= 100) {
    _seen.pop_front();
  }
  _seen.push_back(Seen(src, dst, seq, 0, 0));
  si = _seen.size() - 1;
  
// Backpressure needs to know the path of this request
  _seen[si]._path = path;
  _seen[si]._count++;
  _seen[si]._when = Timestamp::now();
  _seen[si]._p = 0;

  if (dst == _ip) {
    /* don't forward queries for me */
    /* just spit them out the output */
    output(1).push(p_in);
    return;
  }

  _seen[si]._p = p_in->clone();
  
  /* schedule timer */
  int delay_time = click_random(1, 1750);
  sr_assert(delay_time > 0);
  
  _seen[si]._to_send = _seen[si]._when + Timestamp::make_msec(delay_time);
  _seen[si]._forwarded = false;
  Timer *t = new Timer(static_forward_query_hook, (void *) this);
  t->initialize(this);
  t->schedule_after_msec(delay_time);


  output(1).push(p_in);
  return;
  
  


}

void
BPMetricFlood::push(int port, Packet *p_in)
{
  if (port == 0) {
    process_flood(p_in);
  } else if (port == 1) {
    start_flood(p_in);
  } else {
    p_in->kill();
    return;
  }
}


enum {H_DEBUG, H_IP, H_CLEAR, H_FLOODS};

static String 
BPMetricFlood_read_param(Element *e, void *thunk)
{
  BPMetricFlood *td = (BPMetricFlood *)e;
  switch ((uintptr_t) thunk) {
  case H_DEBUG:
    return String(td->_debug) + "\n";
  case H_IP:
    return td->_ip.unparse() + "\n";
  case H_FLOODS: {
	  StringAccum sa;
	  int x;
	  for (x = 0; x < td->_seen.size(); x++) {
		  sa << "src " << td->_seen[x]._src;
		  sa << " dst " << td->_seen[x]._dst;
		  sa << " seq " << td->_seen[x]._seq;
		  sa << " count " << td->_seen[x]._count;
		  sa << " forwarded " << td->_seen[x]._forwarded;
		  sa << " when " << td->_seen[x]._when;
		  sa << " to_send " << td->_seen[x]._to_send;
		  sa << " path (" << path_to_string(td->_seen[x]._path) << ")";
		  sa << "\n";
	  }
	  return sa.take_string();
  }
  default:
    return String();
  }
}
static int 
BPMetricFlood_write_param(const String &in_s, Element *e, void *vparam,
		      ErrorHandler *errh)
{
  BPMetricFlood *f = (BPMetricFlood *)e;
  String s = cp_uncomment(in_s);
  switch((intptr_t)vparam) {
  case H_DEBUG: {    //debug
    bool debug;
    if (!cp_bool(s, &debug)) 
      return errh->error("debug parameter must be boolean");
    f->_debug = debug;
    break;
  }
  case H_CLEAR:
    f->_seen.clear();
    break;
  }
  return 0;
}
void
BPMetricFlood::add_handlers()
{
  add_read_handler("debug", BPMetricFlood_read_param, (void *) H_DEBUG);
  add_read_handler("ip", BPMetricFlood_read_param, (void *) H_IP);
  add_read_handler("floods", BPMetricFlood_read_param, (void *) H_FLOODS);

  add_write_handler("debug", BPMetricFlood_write_param, (void *) H_DEBUG);
  add_write_handler("clear", BPMetricFlood_write_param, (void *) H_CLEAR);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel int64)
EXPORT_ELEMENT(BPMetricFlood)
