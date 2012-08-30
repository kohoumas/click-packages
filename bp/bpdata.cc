/*
 * bpdata.{cc,hh} -- Element that holds the Backpressure related data
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

#include "bpdata.hh"

CLICK_DECLS

BPData::BPData() {}

BPData::~BPData() {}

int BPData::configure (Vector<String> &conf, ErrorHandler *errh) {

  _enhanced = true;
  int res = cp_va_kparse(conf, this, errh,
		    "ENHANCED", 0, cpBool, &_enhanced,
                    cpEnd);
  return res;
}

Flow_BPInfo_Table* BPData::get_table(String neig) {
  Flow_BPInfo_Table **fbp = _NFB.findp(neig);
  Flow_BPInfo_Table *fb = fbp ? *fbp : new Flow_BPInfo_Table() ;
  if (!fbp) _NFB.insert(neig, fb);
  return fb;
}

HostBPInfo* BPData::get_info(String neig, Flow f) {
  Flow_BPInfo_Table **fbp = _NFB.findp(neig);
  if (!fbp) return NULL;
  Flow_BPInfo_Table *fb = *fbp;
  return fb->findp(f);
}


// Setup handlers
String BPData::read_handler(Element *e, void *) {

  BPData *b = static_cast<BPData *>(e);
  String ret = "";

  for(Neig_Flow_BPInfo_Table::iterator nfb_iter = b->get_iterator(); nfb_iter.live(); nfb_iter++) {
    ret += nfb_iter.key() + " : " + String(*(b->get_metric(nfb_iter.key()))) + " ; ";
    for(Flow_BPInfo_Table::iterator fb_iter = nfb_iter.value()->begin(); fb_iter.live(); fb_iter++) {
      if(b->_enhanced)
        ret += fb_iter.key()._dst.s() + " " + String(fb_iter.value()._backlog) + " " + String(fb_iter.value()._distance) + " ; ";
      else
        ret += fb_iter.key()._dst.s() + " " + String(fb_iter.value()._backlog) + " ; ";
    }
    ret += "\n";
  }

  return ret;
}

int BPData::write_handler(const String &in_s, Element *e, void *vparam, ErrorHandler *errh) {

  BPData *b =  static_cast<BPData *>(e);
  Vector<String> args;
  cp_spacevec(in_s, args);

  String neig = args[0];
  Flow f = Flow(IPAddress(args[1]));

  HostBPInfo *neig_info = b->get_info(neig, f);
  neig_info->_backlog = atoi(args[2].c_str());
  neig_info->_distance = atoi(args[3].c_str());
  neig_info->_static_distance = true;

  return 0;
}

void BPData::add_handlers() {
  add_read_handler("info", read_handler, 0);
  add_write_handler("info", write_handler, 0);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(BPData)

