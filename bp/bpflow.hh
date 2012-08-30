/*
 * bpdata.hh -- Class that defines the Backpressure flow
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

#ifndef BPFLOW_HH
#define BPFLOW_HH

#include <click/ipaddress.hh>

CLICK_DECLS

class Flow {
public:

  IPAddress _dst;

  Flow() {}
  Flow(IPAddress dst) : _dst(dst) {}

  inline bool operator==(Flow other)  {return (other._dst == _dst);}
  inline bool contains(IPAddress foo) {return (foo == _dst);}
};

// The following definition is neseccary for building hash tables with Flow keys
inline unsigned hashcode(const Flow &f) {return f._dst.addr();}

CLICK_ENDDECLS

#endif
