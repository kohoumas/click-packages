#ifndef DATAQUEUE_HH
#define DATAQUEUE_HH

#if CLICK_VERSION_CODE >= CLICK_MAKE_VERSION_CODE(2,0,1)
#include <click/deque.hh>
#define QUEUE_CLASS_NAME Deque
#else
#include <click/dequeue.hh>
#define QUEUE_CLASS_NAME DEQueue
#endif

#include <click/packet.hh>
#include <elements/wifi/sr/srpacket.hh>

#include "bpconfig.h"

CLICK_DECLS

class BPQueue {
public:

  BPQueue(String key, enum Layer maclayer, bool video) : _key(key), _maclayer(maclayer), _video(video) {}

  virtual uint32_t size()=0;
  String key() { return _key; }

protected:

  String _key;
  enum Layer _maclayer;
  bool _video;
};


//// Unicast

class DataQueue : public BPQueue {
public:

  Packet* front() { 
    if (_video) {
      if (sizeQI()) {return _QI.front();}
      else if (sizeQP()) {return _QP.front();}
    } 
    return _Q.front();
  }
  Packet* pop_front() { 
    if (_video) {
      if (sizeQI()) {Packet *p = _QI.front(); _QI.pop_front(); return p;}
      else if (sizeQP()) {Packet *p = _QP.front(); _QP.pop_front(); return p;}
    }
    Packet *p = _Q.front(); _Q.pop_front(); return p;
  }
  void drop_front() { 
    add_drop();
    if (_video) {
      Packet *p;
      if (sizeQ()) {p = _Q.front(); _Q.pop_front();}
      else if (sizeQP()) {p = _QP.front(); _QP.pop_front();}
      else {p = _QI.front(); _QI.pop_front();}
      p->kill(); 
      return;
    }
    Packet *p = _Q.front(); _Q.pop_front(); p->kill();
  }
  Packet* back() { return _Q.back(); }
  void push_back(Packet *p) {
    if (_video) {
      uint8_t ether_type_offset = _maclayer == WIFIQOS ? sizeof(click_wifi) + 2 + 6 : // 2 bytes QoS + 6 bytes ether_type offset in LLC
                                 (_maclayer == WIFI ? sizeof(click_wifi) + 6 : 12);
      uint16_t ether_type = *((uint16_t*)p->data() + ether_type_offset);
      uint8_t mac_header_length = _maclayer == WIFIQOS ? sizeof(click_wifi) + 2 + sizeof(click_llc) : 
                                 (_maclayer == WIFI ? sizeof(click_wifi) + sizeof(click_llc) : sizeof(click_ether));
      void *data = NULL;
      if (ether_type == 0x604) {
        struct srpacket *pk = (struct srpacket *)(p->data() + mac_header_length);
        data = (void *)pk->data();
      } 
      else { //if (ether_type == 0x800) {
        data = (void *)(p->data() + mac_header_length);
      }
      //uint32_t iperf_seq = ntohl(*(uint32_t *)(data + sizeof(click_ip) + sizeof(click_udp)));
      //click_chatter("iperf sequence number : %d", iperf_seq);
      uint8_t nal_type = *(uint8_t *)(data + sizeof(click_ip) + sizeof(click_udp) + RTP_HEADER_LENGTH);
      nal_type = nal_type & 0x1F; // The NAL type is the 5 last bits of the first byte of the NAL header
      //click_chatter("H264 NAL type : %d", nal_type);

      switch (nal_type) {
        case 5: // I frame
        case 6: // SEI frame
        case 7: // SPS frame
        case 8: // PPS frame
          _QI.push_back(p);
          return;
        case 1: // P or B frame
          _QP.push_back(p);
          return;
      }
    }
    _Q.push_back(p); 
  }

  uint32_t size() { return _video ? _QI.size() + _QP.size() + _Q.size() : _Q.size(); }
  uint32_t sizeQ() { return _Q.size(); }
  uint32_t sizeQI() { return _QI.size(); }
  uint32_t sizeQP() { return _QP.size(); }

  uint32_t drops() { return _drops; }
  void add_drop() { _drops++; }
  void add_drops(uint32_t drops) { _drops += drops; }
  void remove_drops(uint32_t drops) { _drops = _drops > drops ? _drops - drops : 0 ; }

  uint32_t highwater_length() { return _highwater_length; }
  void set_highwater_length(uint32_t new_value) { _highwater_length = new_value; }

  DataQueue(String key, uint32_t cap, enum Layer maclayer, uint32_t drops, bool video) : BPQueue(key, maclayer, video), _Q(), _QI(), _QP(),
    _drops(drops), _highwater_length(0) { 
    if (video) { _QI.reserve(cap/3); _QP.reserve(cap/3); _Q.reserve(cap/3); }
    else { _Q.reserve(cap); }
  }

private:

  QUEUE_CLASS_NAME<Packet*> _Q;
  QUEUE_CLASS_NAME<Packet*> _QI;
  QUEUE_CLASS_NAME<Packet*> _QP;

  uint32_t _drops;
  uint32_t _highwater_length;
};


//// Multicast

enum Policy {MMT, MMU, PLAIN};

typedef HashMap<EtherAddress, DataQueue*> Ether_Queue_Table;
typedef HashMap<String, uint32_t> Weights_Table;

class DataQueueMulticast : public BPQueue {
public:

  Ether_Queue_Table::iterator get_iterator() { return _EQ.begin(); }
  uint32_t num_queues() { return _EQ.size(); }
  uint32_t size() {
    int32_t s = 0;
    if (_policy == MMU && _dest_queue) {
      s = (_Z >= _zeta) ? (int32_t)(_w * exp(_w * (_Z - _zeta))) : (int32_t)(-_w * exp(_w * (_zeta - _Z)));
    }
    for (Ether_Queue_Table::const_iterator eq_iter = get_iterator(); eq_iter.live(); eq_iter++) {
      if (_policy == PLAIN) {
        s = eq_iter.value()->size();
      } else {
        uint32_t *wp = _weights->findp(eq_iter.value()->key());
        assert(wp);
        s += (*wp) * eq_iter.value()->size();
      }
    }
    //click_chatter("queue %s size %u", _key.c_str(), s);
    return (uint32_t)s;
  }
  uint32_t drops() {
    int32_t d = 0;
    for (Ether_Queue_Table::const_iterator eq_iter = get_iterator(); eq_iter.live(); eq_iter++) {
      d += eq_iter.value()->size();
    }
    //click_chatter("queue %s drops %u", _key.c_str(), d);
    return (uint32_t)d;
  }
  DataQueue* get_queue(EtherAddress eth) { 
    DataQueue **qp = _EQ.findp(eth);
    DataQueue *q = qp ? *qp : new DataQueue(_key + " + " + eth.unparse_colon(), _capacity, _maclayer, (_policy == MMT ? _V : _V * _theta), _video);
    if (!qp) _EQ.insert(eth, q);
    return q;
  }
  void increase_received() { _received++; } // MMU
  void update_q_virtual() { // MMU
    if (!_dest_queue) return;

    float Y = (_Z >= _zeta) ? _w * exp(_w * (_Z - _zeta)) : -_w * exp(_w * (_zeta - _Z));

    // utility function: g(x) = x - K(a-x)^+
    float K = _theta - 1;
    float v = _vmax;
    if (Y < 0.0) v = 0.0;
    else if (Y < K * _V) v = _vmax < _a ? _vmax : _a;

    _Z = v < _Z ? _Z - v : 0 ;

    _Z += _received;
    //click_chatter("Y: %.2f - new _Z: %.2f, _received: %u, v/vmax: %.2f/%.2f", Y, _Z, _received, v, _vmax);
    _received = 0; 
  }

  DataQueueMulticast(String key, bool dest_queue, uint32_t cap, Weights_Table *weights, uint32_t V, enum Layer maclayer, bool video,
      enum Policy policy, float epsilon=0.0, float vmax=0.0, float zeta=0.0, float theta=0.0, float a=0.0) : 
    BPQueue(key, maclayer, video), _dest_queue(dest_queue), _capacity(cap), _weights(weights), _V(V), 
      _policy(policy), _epsilon(epsilon), _vmax(vmax), _zeta(zeta), _theta(theta), _a(a), _received(0) { 

   // deltamax = vmax
   _w = _epsilon/pow(vmax,2.0) * exp(-_epsilon/vmax); 
   _Z = zeta + log(V*theta/_w)/_w; 
  }

private:

  Ether_Queue_Table _EQ;

  bool _dest_queue; // true if the queue does not storage packets (the destination of the correspondent flow is the host of this queue)
  Weights_Table *_weights;
  uint32_t _capacity;
  uint32_t _V;
  enum Policy _policy;
  // The following are for MMU
  float _epsilon;
  float _vmax;
  float _w;
  float _zeta;
  float _theta;
  // The following are for destinations in MMU
  float _a; // utility function
  float _Z; // virtual queue
  uint32_t _received; // number of received (not stored and forwarded) packets in the destination node
};

CLICK_ENDDECLS

#endif
