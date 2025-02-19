#pragma once
/**
	@file
	@brief finite field class
	@author MITSUNARI Shigeo(@herumi)
	@license modified new BSD license
	http://opensource.org/licenses/BSD-3-Clause
*/
#ifndef CYBOZU_DONT_USE_STRING
#include <iosfwd>
#endif
#ifdef _MSC_VER
	#pragma warning(push)
	#pragma warning(disable : 4127)
	#pragma warning(disable : 4458)
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#ifndef MCL_NO_AUTOLINK
		#ifdef NDEBUG
			#pragma comment(lib, "mcl.lib")
		#else
			#pragma comment(lib, "mcl.lib")
		#endif
	#endif
#endif
#include <cybozu/hash.hpp>
#include <cybozu/stream.hpp>
#include <mcl/op.hpp>
#include <mcl/util.hpp>
#include <mcl/operator.hpp>
#include <mcl/conversion.hpp>

namespace mcl {

struct FpTag;
struct ZnTag;

namespace fp {

// copy src to dst as little endian
void copyUnitToByteAsLE(uint8_t *dst, const Unit *src, size_t byteSize);
// copy src to dst as little endian
void copyByteToUnitAsLE(Unit *dst, const uint8_t *src, size_t byteSize);

bool copyAndMask(Unit *y, const void *x, size_t xByteSize, const Op& op, MaskMode maskMode);

uint64_t getUint64(bool *pb, const fp::Block& b);
int64_t getInt64(bool *pb, fp::Block& b, const fp::Op& op);

const char *ModeToStr(Mode mode);

Mode StrToMode(const char *s);

#ifndef CYBOZU_DONT_USE_STRING
inline Mode StrToMode(const std::string& s)
{
	return StrToMode(s.c_str());
}
#endif

inline void dumpUnit(Unit x)
{
#if MCL_SIZEOF_UNIT == 4
	printf("%08x", (uint32_t)x);
#else
	printf("%016llx", (unsigned long long)x);
#endif
}

bool isEnableJIT(); // 1st call is not threadsafe

uint32_t sha256(void *out, uint32_t maxOutSize, const void *msg, uint32_t msgSize);
uint32_t sha512(void *out, uint32_t maxOutSize, const void *msg, uint32_t msgSize);

namespace local {

inline void byteSwap(void *x, size_t n)
{
	char *p = (char *)x;
	for (size_t i = 0; i < n / 2; i++) {
		fp::swap_(p[i], p[n - 1 - i]);
	}
}

} // mcl::fp::local

} // mcl::fp

template<class tag = FpTag, size_t maxBitSize = MCL_MAX_BIT_SIZE>
class FpT : public fp::Serializable<FpT<tag, maxBitSize>,
	fp::Operator<FpT<tag, maxBitSize> > > {
	typedef fp::Unit Unit;
	typedef fp::Operator<FpT<tag, maxBitSize> > Operator;
	typedef fp::Serializable<FpT<tag, maxBitSize>, Operator> Serializer;
public:
	static const size_t maxSize = (maxBitSize + fp::UnitBitSize - 1) / fp::UnitBitSize;
private:
	template<class tag2, size_t maxBitSize2> friend class FpT;
	Unit v_[maxSize];
	static fp::Op op_;
	static FpT<tag, maxBitSize> inv2_;
	static int ioMode_;
	static bool isETHserialization_;
	template<class Fp> friend class FpDblT;
	template<class Fp> friend class Fp2T;
	template<class Fp> friend struct Fp6T;
public:
	typedef FpT<tag, maxBitSize> BaseFp;
	// return pointer to array v_[]
	const Unit *getUnit() const { return v_; }
	FpT* getFp0() { return this; }
	const FpT* getFp0() const { return this; }
	static inline size_t getUnitSize() { return op_.N; }
	static inline size_t getBitSize() { return op_.bitSize; }
	static inline size_t getByteSize() { return (op_.bitSize + 7) / 8; }
	static inline const fp::Op& getOp() { return op_; }
	void dump() const
	{
		const size_t N = op_.N;
		for (size_t i = 0; i < N; i++) {
			fp::dumpUnit(v_[N - 1 - i]);
		}
		printf("\n");
	}
	/*
		xi_a is used for Fp2::mul_xi(), where xi = xi_a + i and i^2 = -1
		if xi_a = 0 then asm functions for Fp2 are not generated.
	*/
	static inline void init(bool *pb, int xi_a, const mpz_class& p, fp::Mode mode = fp::FP_AUTO)
	{
		assert(maxBitSize <= MCL_MAX_BIT_SIZE);
		*pb = op_.init(p, maxBitSize, xi_a, mode);
		if (!*pb) return;
		{ // set oneRep
			FpT& one = *reinterpret_cast<FpT*>(op_.oneRep);
			one.clear();
			one.v_[0] = 1;
			one.toMont();
		}
		{ // set half
			mpz_class half = (op_.mp + 1) / 2;
			gmp::getArray(pb, op_.half, op_.N, half);
			if (!*pb) return;
		}
		inv(inv2_, 2);
		ioMode_ = 0;
		isETHserialization_ = false;
#ifdef MCL_XBYAK_DIRECT_CALL
		add = fp::func_ptr_cast<void (*)(FpT& z, const FpT& x, const FpT& y)>(op_.fp_addA_);
		if (add == 0) add = addC;
		sub = fp::func_ptr_cast<void (*)(FpT& z, const FpT& x, const FpT& y)>(op_.fp_subA_);
		if (sub == 0) sub = subC;
		neg = fp::func_ptr_cast<void (*)(FpT& y, const FpT& x)>(op_.fp_negA_);
		if (neg == 0) neg = negC;
		mul = fp::func_ptr_cast<void (*)(FpT& z, const FpT& x, const FpT& y)>(op_.fp_mulA_);
		if (mul == 0) mul = mulC;
		sqr = fp::func_ptr_cast<void (*)(FpT& y, const FpT& x)>(op_.fp_sqrA_);
		if (sqr == 0) sqr = sqrC;
#endif
		*pb = true;
	}
	static inline void init(bool *pb, const mpz_class& p, fp::Mode mode = fp::FP_AUTO)
	{
		init(pb, 0, p, mode);
	}
	static inline void init(bool *pb, const char *mstr, fp::Mode mode = fp::FP_AUTO)
	{
		mpz_class p;
		gmp::setStr(pb, p, mstr);
		if (!*pb) return;
		init(pb, p, mode);
	}
	static inline size_t getModulo(char *buf, size_t bufSize)
	{
		return gmp::getStr(buf, bufSize, op_.mp);
	}
	static inline bool isFullBit() { return op_.isFullBit; }
	/*
		binary patter of p
		@note the value of p is zero
	*/
	static inline const FpT& getP()
	{
		return *reinterpret_cast<const FpT*>(op_.p);
	}
	bool isOdd() const
	{
		fp::Block b;
		getBlock(b);
		return (b.p[0] & 1) == 1;
	}
	static inline bool squareRoot(FpT& y, const FpT& x)
	{
		if (isMont()) return op_.sq.get(y, x);
		mpz_class mx, my;
		bool b = false;
		x.getMpz(&b, mx);
		if (!b) return false;
		b = op_.sq.get(my, mx);
		if (!b) return false;
		y.setMpz(&b, my);
		return b;
	}
	FpT() {}
	FpT(const FpT& x)
	{
		op_.fp_copy(v_, x.v_);
	}
	FpT& operator=(const FpT& x)
	{
		op_.fp_copy(v_, x.v_);
		return *this;
	}
	void clear()
	{
		op_.fp_clear(v_);
	}
	FpT(int64_t x) { operator=(x); }
	FpT& operator=(int64_t x)
	{
		if (x == 1) {
			op_.fp_copy(v_, op_.oneRep);
		} else {
			clear();
			if (x) {
				int64_t y = x < 0 ? -x : x;
				if (sizeof(Unit) == 8) {
					v_[0] = y;
				} else {
					v_[0] = (uint32_t)y;
					v_[1] = (uint32_t)(y >> 32);
				}
				if (x < 0) neg(*this, *this);
				toMont();
			}
		}
		return *this;
	}
	static inline bool isMont() { return op_.isMont; }
	/*
		convert normal value to Montgomery value
		do nothing is !isMont()
	*/
	void toMont()
	{
		if (isMont()) op_.toMont(v_, v_);
	}
	/*
		convert Montgomery value to normal value
		do nothing is !isMont()
	*/
	void fromMont()
	{
		if (isMont()) op_.fromMont(v_, v_);
	}
	template<class InputStream>
	void load(bool *pb, InputStream& is, int ioMode)
	{
		bool isMinus = false;
		*pb = false;
		if (ioMode & (IoArray | IoArrayRaw | IoSerialize | IoSerializeHexStr)) {
			const size_t n = getByteSize();
			v_[op_.N - 1] = 0;
			size_t readSize;
			if (ioMode & IoSerializeHexStr) {
				readSize = mcl::fp::readHexStr(v_, n, is);
			} else {
				readSize = cybozu::readSome(v_, n, is);
			}
			if (isETHserialization_ && ioMode & (IoSerialize | IoSerializeHexStr)) {
				fp::local::byteSwap(v_, n);
			}
			if (readSize != n) return;
		} else {
			char buf[1024];
			size_t n = fp::local::loadWord(buf, sizeof(buf), is);
			if (n == 0) return;
			n = fp::strToArray(&isMinus, v_, op_.N, buf, n, ioMode);
			if (n == 0) return;
			for (size_t i = n; i < op_.N; i++) v_[i] = 0;
		}
		if (fp::isGreaterOrEqualArray(v_, op_.p, op_.N)) {
			return;
		}
		if (isMinus) {
			neg(*this, *this);
		}
		if (!(ioMode & IoArrayRaw)) {
			toMont();
		}
		*pb = true;
	}
	template<class OutputStream>
	void save(bool *pb, OutputStream& os, int ioMode) const
	{
		const size_t n = getByteSize();
		if (ioMode & (IoArray | IoArrayRaw | IoSerialize | IoSerializeHexStr)) {
			if (ioMode & IoArrayRaw) {
				cybozu::write(pb, os, v_, n);
			} else {
				fp::Block b;
				getBlock(b);
				const char *src = (const char *)b.p;
				char rev[fp::maxUnitSize * sizeof(fp::Unit)];
				if (isETHserialization_ && ioMode & (IoSerialize | IoSerializeHexStr)) {
					for (size_t i = 0; i < n; i++) {
						rev[i] = src[n - 1 - i];
					}
					src = rev;
				}
				if (ioMode & IoSerializeHexStr) {
					mcl::fp::writeHexStr(pb, os, src, n);
				} else {
					cybozu::write(pb, os, src, n);
				}
			}
			return;
		}
		fp::Block b;
		getBlock(b);
		// use low 8-bit ioMode for (base, withPrefix)
		char buf[2048];
		size_t len = mcl::fp::arrayToStr(buf, sizeof(buf), b.p, b.n, ioMode & 31, (ioMode & IoPrefix) != 0);
		if (len == 0) {
			*pb = false;
			return;
		}
		cybozu::write(pb, os, buf + sizeof(buf) - len, len);
	}
	/*
		mode = Mod : set x mod p if sizeof(S) * n <= 64 else error
	*/
	template<class S>
	void setArray(bool *pb, const S *x, size_t n, mcl::fp::MaskMode mode = fp::NoMask)
	{
		*pb = fp::copyAndMask(v_, x, sizeof(S) * n, op_, mode);
		toMont();
	}
	/*
		mask x with (1 << bitLen) and subtract p if x >= p
	*/
	template<class S>
	void setArrayMaskMod(const S *x, size_t n)
	{
		fp::copyAndMask(v_, x, sizeof(S) * n, op_, fp::MaskAndMod);
		toMont();
	}

	/*
		mask x with (1 << (bitLen - 1)) - 1 if x >= p
	*/
	template<class S>
	void setArrayMask(const S *x, size_t n)
	{
		fp::copyAndMask(v_, x, sizeof(S) * n, op_, fp::SmallMask);
		toMont();
	}
	void getBlock(fp::Block& b) const
	{
		b.n = op_.N;
		if (isMont()) {
			op_.fromMont(b.v_, v_);
			b.p = &b.v_[0];
		} else {
			b.p = &v_[0];
		}
	}
	/*
		write a value with little endian
		write buf[0] = 0 and return 1 if the value is 0
		return written size if success else 0
	*/
	size_t getLittleEndian(void *buf, size_t maxBufSize) const
	{
		fp::Block b;
		getBlock(b);
		const uint8_t *src = (const uint8_t *)b.p;
		uint8_t *dst = (uint8_t *)buf;
		size_t n = b.n * sizeof(b.p[0]);
		while (n > 0) {
			if (src[n - 1]) break;
			n--;
		}
		if (n == 0) n = 1; // zero
		if (maxBufSize < n) return 0;
		for (size_t i = 0; i < n; i++) {
			dst[i] = src[i];
		}
		return n;
	}
	void setByCSPRNG(bool *pb, fp::RandGen rg = fp::RandGen())
	{
		if (rg.isZero()) rg = fp::RandGen::get();
		rg.read(pb, v_, op_.N * sizeof(Unit)); // byte size
		if (!pb) return;
		setArrayMask(v_, op_.N);
	}
#ifndef CYBOZU_DONT_USE_EXCEPTION
	void setByCSPRNG(fp::RandGen rg = fp::RandGen())
	{
		bool b;
		setByCSPRNG(&b, rg);
		if (!b) throw cybozu::Exception("setByCSPRNG");
	}
#endif
	void setRand(fp::RandGen rg = fp::RandGen()) // old api
	{
		setByCSPRNG(rg);
	}
	/*
		hash msg and mask with (1 << (bitLen - 1)) - 1
	*/
	void setHashOf(const void *msg, size_t msgSize)
	{
		char buf[MCL_MAX_HASH_BIT_SIZE / 8];
		uint32_t size = op_.hash(buf, static_cast<uint32_t>(sizeof(buf)), msg, static_cast<uint32_t>(msgSize));
		setArrayMask(buf, size);
	}
	void getMpz(bool *pb, mpz_class& x) const
	{
		fp::Block b;
		getBlock(b);
		gmp::setArray(pb, x, b.p, b.n);
	}
	void setMpz(bool *pb, const mpz_class& x)
	{
		if (x < 0) {
			*pb = false;
			return;
		}
		setArray(pb, gmp::getUnit(x), gmp::getUnitSize(x));
	}
#ifdef MCL_XBYAK_DIRECT_CALL
	static void (*add)(FpT& z, const FpT& x, const FpT& y);
	static inline void addC(FpT& z, const FpT& x, const FpT& y) { op_.fp_add(z.v_, x.v_, y.v_, op_.p); }
	static void (*sub)(FpT& z, const FpT& x, const FpT& y);
	static inline void subC(FpT& z, const FpT& x, const FpT& y) { op_.fp_sub(z.v_, x.v_, y.v_, op_.p); }
	static void (*neg)(FpT& y, const FpT& x);
	static inline void negC(FpT& y, const FpT& x) { op_.fp_neg(y.v_, x.v_, op_.p); }
	static void (*mul)(FpT& z, const FpT& x, const FpT& y);
	static inline void mulC(FpT& z, const FpT& x, const FpT& y) { op_.fp_mul(z.v_, x.v_, y.v_, op_.p); }
	static void (*sqr)(FpT& y, const FpT& x);
	static inline void sqrC(FpT& y, const FpT& x) { op_.fp_sqr(y.v_, x.v_, op_.p); }
#else
	static inline void add(FpT& z, const FpT& x, const FpT& y) { op_.fp_add(z.v_, x.v_, y.v_, op_.p); }
	static inline void sub(FpT& z, const FpT& x, const FpT& y) { op_.fp_sub(z.v_, x.v_, y.v_, op_.p); }
	static inline void neg(FpT& y, const FpT& x) { op_.fp_neg(y.v_, x.v_, op_.p); }
	static inline void mul(FpT& z, const FpT& x, const FpT& y) { op_.fp_mul(z.v_, x.v_, y.v_, op_.p); }
	static inline void sqr(FpT& y, const FpT& x) { op_.fp_sqr(y.v_, x.v_, op_.p); }
#endif
	static inline void addPre(FpT& z, const FpT& x, const FpT& y) { op_.fp_addPre(z.v_, x.v_, y.v_); }
	static inline void subPre(FpT& z, const FpT& x, const FpT& y) { op_.fp_subPre(z.v_, x.v_, y.v_); }
	static inline void mulUnit(FpT& z, const FpT& x, const Unit y)
	{
		if (mulSmallUnit(z, x, y)) return;
		op_.fp_mulUnit(z.v_, x.v_, y, op_.p);
	}
	static inline void inv(FpT& y, const FpT& x) { op_.fp_invOp(y.v_, x.v_, op_); }
	static inline void divBy2(FpT& y, const FpT& x)
	{
#if 0
		mul(y, x, inv2_);
#else
		bool odd = (x.v_[0] & 1) != 0;
		op_.fp_shr1(y.v_, x.v_);
		if (odd) {
			op_.fp_addPre(y.v_, y.v_, op_.half);
		}
#endif
	}
	static inline void divBy4(FpT& y, const FpT& x)
	{
		divBy2(y, x); // QQQ : optimize later
		divBy2(y, y);
	}
	bool isZero() const { return op_.fp_isZero(v_); }
	bool isOne() const { return fp::isEqualArray(v_, op_.oneRep, op_.N); }
	static const inline FpT& one() { return *reinterpret_cast<const FpT*>(op_.oneRep); }
	/*
		half = (p + 1) / 2
		return true if half <= x < p
		return false if 0 <= x < half
	*/
	bool isNegative() const
	{
		fp::Block b;
		getBlock(b);
		return fp::isGreaterOrEqualArray(b.p, op_.half, op_.N);
	}
	bool isValid() const
	{
		return fp::isLessArray(v_, op_.p, op_.N);
	}
	uint64_t getUint64(bool *pb) const
	{
		fp::Block b;
		getBlock(b);
		return fp::getUint64(pb, b);
	}
	int64_t getInt64(bool *pb) const
	{
		fp::Block b;
		getBlock(b);
		return fp::getInt64(pb, b, op_);
	}
	bool operator==(const FpT& rhs) const { return fp::isEqualArray(v_, rhs.v_, op_.N); }
	bool operator!=(const FpT& rhs) const { return !operator==(rhs); }
	/*
		@note
		this compare functions is slow because of calling mul if isMont is true.
	*/
	static inline int compare(const FpT& x, const FpT& y)
	{
		fp::Block xb, yb;
		x.getBlock(xb);
		y.getBlock(yb);
		return fp::compareArray(xb.p, yb.p, op_.N);
	}
	bool isLess(const FpT& rhs) const
	{
		fp::Block xb, yb;
		getBlock(xb);
		rhs.getBlock(yb);
		return fp::isLessArray(xb.p, yb.p, op_.N);
	}
	bool operator<(const FpT& rhs) const { return isLess(rhs); }
	bool operator>=(const FpT& rhs) const { return !operator<(rhs); }
	bool operator>(const FpT& rhs) const { return rhs < *this; }
	bool operator<=(const FpT& rhs) const { return !operator>(rhs); }
	/*
		@note
		return unexpected order if isMont is set.
	*/
	static inline int compareRaw(const FpT& x, const FpT& y)
	{
		return fp::compareArray(x.v_, y.v_, op_.N);
	}
	bool isLessRaw(const FpT& rhs) const
	{
		return fp::isLessArray(v_, rhs.v_, op_.N);
	}
	/*
		set IoMode for operator<<(), or operator>>()
	*/
	static inline void setIoMode(int ioMode)
	{
		ioMode_ = ioMode;
	}
	static void setETHserialization(bool ETHserialization)
	{
		if (getBitSize() != 381) return;
		isETHserialization_ = ETHserialization;
	}
	static inline bool isETHserialization() { return isETHserialization_; }
	static inline int getIoMode() { return ioMode_; }
	static inline size_t getModBitLen() { return getBitSize(); }
	static inline void setHashFunc(uint32_t hash(void *out, uint32_t maxOutSize, const void *msg, uint32_t msgSize))
	{
		op_.hash = hash;
	}
#ifndef CYBOZU_DONT_USE_STRING
	explicit FpT(const std::string& str, int base = 0)
	{
		Serializer::setStr(str, base);
	}
	static inline void getModulo(std::string& pstr)
	{
		gmp::getStr(pstr, op_.mp);
	}
	static std::string getModulo()
	{
		std::string s;
		getModulo(s);
		return s;
	}
	void setHashOf(const std::string& msg)
	{
		setHashOf(msg.data(), msg.size());
	}
	// backward compatibility
	static inline void setModulo(const std::string& mstr, fp::Mode mode = fp::FP_AUTO)
	{
		init(mstr, mode);
	}
	friend inline std::ostream& operator<<(std::ostream& os, const FpT& self)
	{
		self.save(os, fp::detectIoMode(getIoMode(), os));
		return os;
	}
	friend inline std::istream& operator>>(std::istream& is, FpT& self)
	{
		self.load(is, fp::detectIoMode(getIoMode(), is));
		return is;
	}
#endif
#ifndef CYBOZU_DONT_USE_EXCEPTION
	static inline void init(int xi_a, const mpz_class& p, fp::Mode mode = fp::FP_AUTO)
	{
		bool b;
		init(&b, xi_a, p, mode);
		if (!b) throw cybozu::Exception("Fp:init");
	}
	static inline void init(int xi_a, const std::string& mstr, fp::Mode mode = fp::FP_AUTO)
	{
		mpz_class p;
		gmp::setStr(p, mstr);
		init(xi_a, p, mode);
	}
	static inline void init(const mpz_class& p, fp::Mode mode = fp::FP_AUTO)
	{
		init(0, p, mode);
	}
	static inline void init(const std::string& mstr, fp::Mode mode = fp::FP_AUTO)
	{
		init(0, mstr, mode);
	}
	template<class OutputStream>
	void save(OutputStream& os, int ioMode = IoSerialize) const
	{
		bool b;
		save(&b, os, ioMode);
		if (!b) throw cybozu::Exception("fp:save") << ioMode;
	}
	template<class InputStream>
	void load(InputStream& is, int ioMode = IoSerialize)
	{
		bool b;
		load(&b, is, ioMode);
		if (!b) throw cybozu::Exception("fp:load") << ioMode;
	}
	/*
		throw exception if x >= p
	*/
	template<class S>
	void setArray(const S *x, size_t n)
	{
		bool b;
		setArray(&b, x, n);
		if (!b) throw cybozu::Exception("Fp:setArray");
	}
	void setMpz(const mpz_class& x)
	{
		bool b;
		setMpz(&b, x);
		if (!b) throw cybozu::Exception("Fp:setMpz");
	}
	uint64_t getUint64() const
	{
		bool b;
		uint64_t v = getUint64(&b);
		if (!b) throw cybozu::Exception("Fp:getUint64:large value");
		return v;
	}
	int64_t getInt64() const
	{
		bool b;
		int64_t v = getInt64(&b);
		if (!b) throw cybozu::Exception("Fp:getInt64:large value");
		return v;
	}
	void getMpz(mpz_class& x) const
	{
		bool b;
		getMpz(&b, x);
		if (!b) throw cybozu::Exception("Fp:getMpz");
	}
	mpz_class getMpz() const
	{
		mpz_class x;
		getMpz(x);
		return x;
	}
#endif
};

template<class tag, size_t maxBitSize> fp::Op FpT<tag, maxBitSize>::op_;
template<class tag, size_t maxBitSize> FpT<tag, maxBitSize> FpT<tag, maxBitSize>::inv2_;
template<class tag, size_t maxBitSize> int FpT<tag, maxBitSize>::ioMode_ = IoAuto;
template<class tag, size_t maxBitSize> bool FpT<tag, maxBitSize>::isETHserialization_ = false;
#ifdef MCL_XBYAK_DIRECT_CALL
template<class tag, size_t maxBitSize> void (*FpT<tag, maxBitSize>::add)(FpT& z, const FpT& x, const FpT& y);
template<class tag, size_t maxBitSize> void (*FpT<tag, maxBitSize>::sub)(FpT& z, const FpT& x, const FpT& y);
template<class tag, size_t maxBitSize> void (*FpT<tag, maxBitSize>::neg)(FpT& y, const FpT& x);
template<class tag, size_t maxBitSize> void (*FpT<tag, maxBitSize>::mul)(FpT& z, const FpT& x, const FpT& y);
template<class tag, size_t maxBitSize> void (*FpT<tag, maxBitSize>::sqr)(FpT& y, const FpT& x);
#endif

} // mcl

#ifndef CYBOZU_DONT_USE_EXCEPTION
#ifdef CYBOZU_USE_BOOST
namespace mcl {

template<class tag, size_t maxBitSize>
size_t hash_value(const mcl::FpT<tag, maxBitSize>& x, size_t v = 0)
{
	return static_cast<size_t>(cybozu::hash64(x.getUnit(), x.getUnitSize(), v));
}

}
#else
namespace std { CYBOZU_NAMESPACE_TR1_BEGIN

template<class tag, size_t maxBitSize>
struct hash<mcl::FpT<tag, maxBitSize> > {
	size_t operator()(const mcl::FpT<tag, maxBitSize>& x, uint64_t v = 0) const
	{
		return static_cast<size_t>(cybozu::hash64(x.getUnit(), x.getUnitSize(), v));
	}
};

CYBOZU_NAMESPACE_TR1_END } // std::tr1
#endif
#endif

#ifdef _MSC_VER
	#pragma warning(pop)
#endif
