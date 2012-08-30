/*
 * bpforwarder.{cc,hh} -- Backpressure forwarding
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
#include <click/ipaddress.hh>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/straccum.hh>
#include <click/packet_anno.hh>
#include <clicknet/ether.h>
#include "bpforwarder.hh"
#include <elements/wifi/sr/srpacket.hh>
#include "elements/ethernet/arptable.hh"
#include <click/packet_anno.hh>
CLICK_DECLS


BPForwarder::BPForwarder()
  :  _ip(),
     _eth(),
     _et(0),
     _datas(0), 
     _databytes(0),
     _link_table(0),
     _arp_table(0)
{

  static unsigned char bcast_addr[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
  _bcast = EtherAddress(bcast_addr);
}

BPForwarder::~BPForwarder()
{
}


int
BPForwarder::configure (Vector<String> &conf, ErrorHandler *errh)
{
  int res;
  res = cp_va_kparse(conf, this, errh,
		     "ETHTYPE", 0, cpUnsigned, &_et,
		     "IP", 0, cpIPAddress, &_ip,
		     "ETH", 0, cpEtherAddress, &_eth,
		     "ARP", 0, cpElement, &_arp_table,
		     "DQ", 0, cpElement, &_dataqueues,
		     /* below not required */
		     "LT", 0, cpElement, &_link_table,
		     cpEnd);

  if (!_et) 
    return errh->error("ETHTYPE not specified");
  if (!_ip) 
    return errh->error("IP not specified");
  if (!_eth) 
    return errh->error("ETH not specified");
  if (!_arp_table) 
    return errh->error("ARP not specified");
  if (!_dataqueues)
    return errh->error("DQ not specified");

  if (_arp_table->cast("ARPTable") == 0) 
    return errh->error("ARP element is not a ARPTable");

  if (_link_table && _link_table->cast("LinkTable") == 0) 
    return errh->error("LT element is not a LinkTable");

  if (res < 0) {
    return res;
  }
  return res;
}

int
BPForwarder::initialize (ErrorHandler *)
{
  return 0;
}

bool
BPForwarder::update_link(IPAddress from, IPAddress to, 
			 uint32_t seq, uint32_t age, uint32_t metric) 
{
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




Packet *
BPForwarder::encap(Packet *p_in, Vector<IPAddress> r, int flags)
{
  sr_assert(r.size() > 1);
  int hops = r.size() - 1;
  unsigned extra = srpacket::len_wo_data(hops) + sizeof(click_ether);
  unsigned payload_len = p_in->length();
  uint16_t ether_type = htons(_et);

  WritablePacket *p = p_in->push(extra);

  assert(extra + payload_len == p_in->length());


  int next = index_of(r, _ip) + 1;
  if (next < 0 || next >= r.size()) {
    click_chatter("%{element}: encap couldn't find %s (%d) in path %s",
		  this,
		  _ip.unparse().c_str(),
		  next,
		  path_to_string(r).c_str());
    p_in->kill();
    return (0);
  }
  EtherAddress eth_dest = _arp_table->lookup(r[next]);
  /*if (eth_dest.is_broadcast()) {
    click_chatter("%{element}: arp lookup failed for %s",
		  this,
		  r[next].unparse().c_str());
  }*/

  memcpy(p->data(), eth_dest.data(), 6);
  memcpy(p->data() + 6, _eth.data(), 6);
  memcpy(p->data() + 12, &ether_type, 2);
    

  struct srpacket *pk = (struct srpacket *) (p->data() + sizeof(click_ether));
  memset(pk, '\0', srpacket::len_wo_data(hops));

  pk->_version = _sr_version;
  pk->_type = PT_DATA;
  pk->set_data_len(payload_len);

  pk->set_num_links(hops);
  pk->set_next(next);
  pk->set_flag(flags);
  for(int i = 0; i < hops; i++) {
    pk->set_link_node(i, r[i]);
  }

  pk->set_link_node(hops, r[r.size()-1]);
  pk->set_data_seq(0);
  
  /* set the ip header anno */
  const click_ip *ip = reinterpret_cast<const click_ip *>
    (p->data() + pk->hlen_wo_data() + sizeof(click_ether));
  p->set_ip_header(ip, sizeof(click_ip));
  return p;
}

// Backpressure
Packet *
BPForwarder::add_link(Packet *p_in, IPAddress nxt)
{
  click_ether *eh = (click_ether *) p_in->data();
  srpacket *pk = (srpacket *) (eh+1);

  int headers_length = sizeof(click_ether) + pk->hlen_wo_data();
  WritablePacket *wp = p_in->push(20); // 20 bytes is the required space in SR header for each link
  memcpy(wp->data(), p_in->data() + 20, headers_length); 
  memset(((char*)wp->data()) + headers_length, 0, 20);

  eh = (click_ether *) wp->data();
  pk = (srpacket *) (eh+1);
  pk->set_num_links(pk->num_links() + 1);
  pk->set_link_node(pk->num_links(), nxt);
  pk->set_next(pk->next() + 1);

  EtherAddress edst = _arp_table->lookup(nxt);
  /*if (edst.is_broadcast()) {
    click_chatter("%{element}::%s arp lookup failed for %s",
		  this,
		  __func__,
		  nxt.unparse().c_str());
  }*/
  memcpy(eh->ether_dhost, edst.data(), 6);
  memcpy(eh->ether_shost, _eth.data(), 6);

  return wp;
}

void
BPForwarder::push(int port, Packet *p_in)
{

  WritablePacket *p = p_in->uniqueify();

  if (!p) {
    return;
  }

  if (port > 1) {
    p->kill();
    return;
  }
  click_ether *eh = (click_ether *) p->data();
  EtherAddress edst = EtherAddress(eh->ether_dhost);
  struct srpacket *pk = (struct srpacket *) (eh+1);


  if (pk->_type != PT_DATA) {
    click_chatter("BPForwarder %s: bad packet_type %04x",
                  _ip.unparse().c_str(),
                  pk->_type);
    p->kill();
    return ;
  }



  if(port == 0 && pk->get_link_node(pk->next()) != _ip){
	  if (edst != _bcast) {
		  /* 
		   * if the arp doesn't have a ethernet address, it
		   * will broadcast the packet. in this case,
		   * don't complain. But otherwise, something's up
		   */
		  click_chatter("%{element}: data not for me seq %d %d/%d ip %s eth %s",
				pk->data_seq(),
				pk->next(),
				pk->num_links(),
				pk->get_link_node(pk->next()).unparse().c_str(),
				edst.unparse().c_str());
	  }
    p->kill();
    return;
  }

  if (1) {
	  /* update the metrics from the packet */
	  IPAddress r_from = pk->get_random_from();
	  IPAddress r_to = pk->get_random_to();
	  uint32_t r_fwd_metric = pk->get_random_fwd_metric();
	  uint32_t r_rev_metric = pk->get_random_rev_metric();
	  uint32_t r_seq = pk->get_random_seq();
	  uint32_t r_age = pk->get_random_age();
	  
	  if (r_from && r_to) {
		  if (r_fwd_metric && !update_link(r_from, r_to, r_seq, r_age, r_fwd_metric)) {
			  click_chatter("%{element} couldn't update r_fwd %s > %d > %s\n",
					this,
					r_from.unparse().c_str(),
					r_fwd_metric,
					r_to.unparse().c_str());
		  }
		  if (r_rev_metric && !update_link(r_to, r_from, r_seq, r_age, r_rev_metric)) {
			  click_chatter("%{element} couldn't update r_rev %s > %d > %s\n",
					this,
					r_to.unparse().c_str(),
					r_rev_metric, 
					r_from.unparse().c_str());
		  }
	  }

	  for(int i = 0; i < pk->num_links(); i++) {
		  IPAddress a = pk->get_link_node(i);
		  IPAddress b = pk->get_link_node(i+1);
		  uint32_t fwd_m = pk->get_link_fwd(i);
		  uint32_t rev_m = pk->get_link_rev(i);
		  uint32_t seq = pk->get_link_seq(i);
		  uint32_t age = pk->get_link_age(i);
		  
		  if (fwd_m && !update_link(a,b,seq,age,fwd_m)) {
			  click_chatter("%{element} couldn't update fwd_m %s > %d > %s\n",
					this,
					a.unparse().c_str(),
					fwd_m,
					b.unparse().c_str());
		  }
		  if (rev_m && !update_link(b,a,seq,age,rev_m)) {
			  click_chatter("%{element} couldn't update rev_m %s > %d > %s\n",
					this,
					b.unparse().c_str(),
					rev_m,
					a.unparse().c_str());
		  }
	  }
  }
  

  /* set the ip header anno */
  const click_ip *ip = reinterpret_cast<const click_ip *>
    (pk->data());
  p->set_ip_header(ip, sizeof(click_ip));
// Backpressure
  p->set_dst_ip_anno(ip->ip_dst); //it is useful for the following BPQuerier element
  
//SRCR
  //if(pk->next() == pk->num_links()){
//Backpressure
  if(p->dst_ip_anno() == _ip){
    // I'm the ultimate consumer of this data.
    /*
     * set the dst to the gateway it came from 
     * this is kinda weird.
     */
    SET_MISC_IP_ANNO(p, pk->get_link_node(0));
    output(1).push(p);
    return;
  } 

  if (1) {
	  IPAddress prev = pk->get_link_node(pk->next()-1);
	  _arp_table->insert(prev, EtherAddress(eh->ether_shost));
	  uint32_t prev_fwd_metric = (_link_table) ? _link_table->get_link_metric(prev, _ip) : 0;
	  uint32_t prev_rev_metric = (_link_table) ? _link_table->get_link_metric(_ip, prev) : 0;
	  
	  uint32_t seq = (_link_table) ? _link_table->get_link_seq(_ip, prev) : 0;
	  uint32_t age = (_link_table) ? _link_table->get_link_age(_ip, prev) : 0;
	  
	  pk->set_link(pk->next()-1,
		       pk->get_link_node(pk->next()-1), _ip,
		       prev_fwd_metric, prev_rev_metric,
		       seq,age);
  }

// SRCR
  /*pk->set_next(pk->next() + 1);
  IPAddress nxt = pk->get_link_node(pk->next());
  
  edst = _arp_table->lookup(nxt);
  if (edst.is_broadcast()) {
    click_chatter("%{element}::%s arp lookup failed for %s",
		  this,
		  __func__,
		  nxt.unparse().c_str());
  }
  memcpy(eh->ether_dhost, edst.data(), 6);
  memcpy(eh->ether_shost, _eth.data(), 6);*/

  output(0).push(p);
  return;


}

String
BPForwarder::static_print_stats(Element *f, void *)
{
  BPForwarder *d = (BPForwarder *) f;
  return d->print_stats();
}

String
BPForwarder::print_stats()
{
  
  return
    String(_datas) + " datas sent\n" +
    String(_databytes) + " bytes of data sent\n";

}

void
BPForwarder::add_handlers()
{
  add_read_handler("stats", static_print_stats, 0);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel int64)
EXPORT_ELEMENT(BPForwarder)
