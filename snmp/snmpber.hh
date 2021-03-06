#ifndef CLICK_SNMPBER_HH
#define CLICK_SNMPBER_HH
#include "snmpbasics.hh"
#include <click/straccum.hh>

class SNMPBEREncoder { public:

    enum Status {
	ERR_OK = 0,
	ERR_INVALID = -1,
	ERR_MEM = -2,
	ERR_TOOBIG = -3
    };

    SNMPBEREncoder();

    inline const unsigned char *data() const;
    inline int length() const;
    inline bool memory_error() const;

    unsigned char *extend(int);

    Status push_sequence(SNMPTag);
    inline Status push_sequence();
    Status push_long_sequence(SNMPTag);
    inline Status push_long_sequence();
    Status pop_sequence();
    Status abort_sequence();		// always returns ERR_INVALID

    Status encode_snmp_oid(const SNMPOid &);

    Status encode_octet_string(SNMPTag, const unsigned char *, int);
    inline Status encode_octet_string(const unsigned char *, int);
    inline Status encode_octet_string(const char *, int);
    inline Status encode_octet_string(const String &);

    Status encode_null();

    inline Status encode_integer(SNMPTag tag, int x) {
	return encode_integer(tag, x, true);
    }
    inline Status encode_integer(SNMPTag tag, unsigned x) {
	return encode_integer(tag, x, false);
    }
    inline Status encode_integer(SNMPTag tag, long x) {
	return encode_integer(tag, x, true);
    }
    inline Status encode_integer(SNMPTag tag, unsigned long x) {
	return encode_integer(tag, x, false);
    }
#if HAVE_INT64_TYPES && !HAVE_INT64_IS_LONG
    inline Status encode_integer(SNMPTag tag, int64_t x) {
	return encode_integer(tag, x, true);
    }
    inline Status encode_integer(SNMPTag tag, uint64_t x) {
	return encode_integer(tag, x, false);
    }
#endif

    inline Status encode_integer(int x) {
	return encode_integer(SNMP_TAG_INTEGER, x);
    }
    inline Status encode_integer(unsigned x) {
	return encode_integer(SNMP_TAG_INTEGER, x);
    }
    inline Status encode_integer(long x) {
	return encode_integer(SNMP_TAG_INTEGER, x);
    }
    inline Status encode_integer(unsigned long x) {
	return encode_integer(SNMP_TAG_INTEGER, x);
    }
#if HAVE_INT64_TYPES && !HAVE_INT64_IS_LONG
    inline Status encode_integer(int64_t x) {
	return encode_integer(SNMP_TAG_INTEGER, x);
    }
    inline Status encode_integer(uint64_t x) {
	return encode_integer(SNMP_TAG_INTEGER, x);
    }
#endif

    Status encode_ip_address(IPAddress);
    Status encode_time_ticks(unsigned);

    // SNMPBEREncoder &operator<<(SNMPBEREncoder &, const SNMPOid &);
    // SNMPBEREncoder &operator<<(SNMPBEREncoder &, const String &);

    enum {
	LEN_1_MAX = 0x7F,
	LEN_2_MAX = 0xFF,
	LEN_3_MAX = 0xFFFF
    };

  private:

    StringAccum _sa;
    Vector<int> _sequence_start;
    Vector<int> _sequence_len_len;
    Status _mem_err;

    static inline int calculate_len_len(int);
    static unsigned char *encode_len(unsigned char *, int);
    static void encode_len_by_len_len(unsigned char *, int, int);
    Status encode_integer(SNMPTag tag, String::uint_large_t x, bool as_signed);

};


inline const unsigned char *
SNMPBEREncoder::data() const
{
    return (const unsigned char *)_sa.data();
}

inline int
SNMPBEREncoder::length() const
{
    return _sa.length();
}

inline bool
SNMPBEREncoder::memory_error() const
{
    return _mem_err < 0;
}

inline SNMPBEREncoder::Status
SNMPBEREncoder::push_sequence()
{
  return push_sequence(SNMP_TAG_SEQUENCE);
}

inline SNMPBEREncoder::Status
SNMPBEREncoder::push_long_sequence()
{
  return push_long_sequence(SNMP_TAG_SEQUENCE);
}

inline SNMPBEREncoder::Status
SNMPBEREncoder::encode_octet_string(const unsigned char *s, int len)
{
  return encode_octet_string(SNMP_TAG_OCTET_STRING, s, len);
}

inline SNMPBEREncoder::Status
SNMPBEREncoder::encode_octet_string(const char *s, int len)
{
  return encode_octet_string((const unsigned char *)s, len);
}

inline SNMPBEREncoder::Status
SNMPBEREncoder::encode_octet_string(const String &str)
{
  return encode_octet_string(str.data(), str.length());
}

inline SNMPBEREncoder::Status
SNMPBEREncoder::encode_ip_address(IPAddress a)
{
  return encode_octet_string(SNMP_TAG_IPADDRESS, a.data(), 4);
}

inline SNMPBEREncoder::Status
SNMPBEREncoder::encode_time_ticks(unsigned tt)
{
  return encode_integer(SNMP_TAG_TIMETICKS, tt);
}

inline int
SNMPBEREncoder::calculate_len_len(int len)
{
  if (len <= LEN_1_MAX)
    return 1;
  else if (len <= LEN_2_MAX)
    return 2;
  else if (len <= LEN_3_MAX)
    return 3;
  else
    return ERR_TOOBIG;
}

inline SNMPBEREncoder &
operator<<(SNMPBEREncoder &ber, const SNMPOid &oid)
{
    ber.encode_snmp_oid(oid);
    return ber;
}

inline SNMPBEREncoder &
operator<<(SNMPBEREncoder &ber, const String &octet_string)
{
    ber.encode_octet_string(octet_string);
    return ber;
}

#endif
