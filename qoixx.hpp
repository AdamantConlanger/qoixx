#ifndef QOIXX_HPP_INCLUDED_
#define QOIXX_HPP_INCLUDED_

#include<cstdint>
#include<cstddef>
#include<cstring>
#include<vector>
#include<type_traits>
#include<memory>
#include<stdexcept>
#include<bit>

#ifndef QOIXX_NO_SIMD
#if defined(__ARM_NEON)
#include<arm_neon.h>
#elif defined(__AVX2__)
#include<immintrin.h>
#endif
#endif

namespace qoixx{

namespace detail{

template<typename T>
struct default_container_operator;

template<typename T, typename A>
requires(sizeof(T) == 1)
struct default_container_operator<std::vector<T, A>>{
  using target_type = std::vector<T, A>;
  static inline target_type construct(std::size_t size){
    target_type t(size);
    return t;
  }
  struct pusher{
    static constexpr bool is_contiguous = true;
    target_type* t;
    std::size_t i = 0;
    inline void push(std::uint8_t x)noexcept{
      (*t)[i++] = static_cast<T>(x);
    }
    inline target_type finalize()noexcept{
      t->resize(i);
      return std::move(*t);
    }
    inline std::uint8_t* raw_pointer()noexcept{
      return reinterpret_cast<std::uint8_t*>(t->data())+i;
    }
    inline void advance(std::size_t n)noexcept{
      i += n;
    }
  };
  static constexpr pusher create_pusher(target_type& t)noexcept{
    return {&t};
  }
  struct puller{
    static constexpr bool is_contiguous = true;
    const T* t;
    inline std::uint8_t pull()noexcept{
      return static_cast<std::uint8_t>(*t++);
    }
    inline const std::uint8_t* raw_pointer()noexcept{
      return reinterpret_cast<const std::uint8_t*>(t);
    }
    inline void advance(std::size_t n)noexcept{
      t += n;
    }
  };
  static constexpr puller create_puller(const target_type& t)noexcept{
    return {t.data()};
  }
  static inline std::size_t size(const target_type& t)noexcept{
    return t.size();
  }
  static constexpr bool valid(const target_type&)noexcept{
    return true;
  }
};

template<typename T>
requires(sizeof(T) == 1)
struct default_container_operator<std::pair<std::unique_ptr<T[]>, std::size_t>>{
  using target_type = std::pair<std::unique_ptr<T[]>, std::size_t>;
  static inline target_type construct(std::size_t size){
    return {typename target_type::first_type{static_cast<T*>(::operator new(size))}, 0};
  }
  struct pusher{
    static constexpr bool is_contiguous = true;
    target_type* t;
    inline void push(std::uint8_t x)noexcept{
      t->first[t->second++] = static_cast<T>(x);
    }
    inline target_type finalize()noexcept{
      return std::move(*t);
    }
    inline std::uint8_t* raw_pointer()noexcept{
      return reinterpret_cast<std::uint8_t*>(t->first.get())+t->second;
    }
    inline void advance(std::size_t n)noexcept{
      t->second += n;
    }
  };
  static constexpr pusher create_pusher(target_type& t)noexcept{
    return {&t};
  }
  struct puller{
    static constexpr bool is_contiguous = true;
    const T* t;
    inline std::uint8_t pull()noexcept{
      return static_cast<std::uint8_t>(*t++);
    }
    inline const std::uint8_t* raw_pointer()noexcept{
      return reinterpret_cast<const std::uint8_t*>(t);
    }
    inline void advance(std::size_t n)noexcept{
      t += n;
    }
  };
  static constexpr puller create_puller(const target_type& t)noexcept{
    return {t.first.get()};
  }
  static inline std::size_t size(const target_type& t)noexcept{
    return t.second;
  }
  static constexpr bool valid(const target_type&)noexcept{
    return true;
  }
};

template<typename T>
requires(sizeof(T) == 1)
struct default_container_operator<std::pair<T*, std::size_t>>{
  using target_type = std::pair<T*, std::size_t>;
  struct puller{
    static constexpr bool is_contiguous = true;
    const T* ptr;
    inline std::uint8_t pull()noexcept{
      return static_cast<std::uint8_t>(*ptr++);
    }
    inline const std::uint8_t* raw_pointer()noexcept{
      return reinterpret_cast<const std::uint8_t*>(ptr);
    }
    inline void advance(std::size_t n)noexcept{
      ptr += n;
    }
  };
  static constexpr puller create_puller(const target_type& t)noexcept{
    return {t.first};
  }
  static inline std::size_t size(const target_type& t)noexcept{
    return t.second;
  }
  static inline bool valid(const target_type& t)noexcept{
    return t.first != nullptr;
  }
};

}

template<typename T>
struct container_operator : detail::default_container_operator<T>{};

class qoi{
  template<std::size_t Size>
  static inline void efficient_memcpy(void* dst, const void* src){
    if constexpr(Size == 3){
      std::memcpy(dst, src, 2);
      std::memcpy(static_cast<std::byte*>(dst)+2, static_cast<const std::byte*>(src)+2, 1);
    }
    else
      std::memcpy(dst, src, Size);
  }
  template<std::size_t Size, typename T>
  static inline void push(T& dst, const void* src){
    if constexpr(T::is_contiguous){
      auto*const ptr = dst.raw_pointer();
      dst.advance(Size);
      efficient_memcpy<Size>(ptr, src);
    }
    else{
      const auto* ptr = static_cast<const std::uint8_t*>(src);
      auto size = Size;
      while(size --> 0)
        dst.push(*ptr++);
    }
  }
  template<std::size_t Size, typename T>
  static inline void pull(void* dst, T& src){
    if constexpr(T::is_contiguous){
      const auto*const ptr = src.raw_pointer();
      src.advance(Size);
      efficient_memcpy<Size>(dst, ptr);
    }
    else{
      auto* ptr = static_cast<std::uint8_t*>(dst);
      auto size = Size;
      while(size --> 0)
        *ptr++ = src.pull();
    }
  }
  enum chunk_tag : std::uint32_t{
    index = 0b0000'0000u,
    diff  = 0b0100'0000u,
    luma  = 0b1000'0000u,
    run   = 0b1100'0000u,
    rgb   = 0b1111'1110u,
    rgba  = 0b1111'1111u,
  };
  static constexpr std::size_t index_size = 64u;
 public:
  enum class colorspace : std::uint8_t{
    srgb = 0,
    linear = 1,
  };
  struct desc{
    std::uint32_t width;
    std::uint32_t height;
    std::uint8_t channels;
    qoi::colorspace colorspace;
  };
  struct rgba_t{
    std::uint8_t r, g, b, a;
    inline std::uint32_t v()const{
      static_assert(sizeof(rgba_t) == sizeof(std::uint32_t));
      if constexpr(std::endian::native == std::endian::little){
        std::uint32_t x;
        std::memcpy(&x, this, sizeof(std::uint32_t));
        return x;
      }
      else
        return std::uint32_t{r}       |
               std::uint32_t{g} <<  8 |
               std::uint32_t{b} << 16 |
               std::uint32_t{a} << 24;
    }
    inline auto hash()const{
      static constexpr std::uint64_t constant =
        static_cast<std::uint64_t>(3u) << 56 |
                                   5u  << 16 |
        static_cast<std::uint64_t>(7u) << 40 |
                                  11u;
      const auto v = static_cast<std::uint64_t>(this->v());
      return (((v<<32|v)&0xFF00FF0000FF00FF)*constant)>>56;
    }
    inline bool operator==(const rgba_t& rhs)const{
      return v() == rhs.v();
    }
    inline bool operator!=(const rgba_t& rhs)const{
      return v() != rhs.v();
    }
  };
  static constexpr std::uint32_t magic = 
    113u /*q*/ << 24 | 111u /*o*/ << 16 | 105u /*i*/ <<  8 | 102u /*f*/ ;
  static constexpr std::size_t header_size =
    sizeof(magic) +
    sizeof(std::declval<desc>().width) +
    sizeof(std::declval<desc>().height) +
    sizeof(std::declval<desc>().channels) +
    sizeof(std::declval<desc>().colorspace);
  static constexpr std::size_t pixels_max = 400000000u;
  static constexpr std::uint8_t padding[8] = {0, 0, 0, 0, 0, 0, 0, 1};
  template<typename Puller>
  static inline std::uint32_t read_32(Puller& p){
    if constexpr(std::endian::native == std::endian::big && Puller::is_contiguous){
      std::uint32_t x;
      pull<sizeof(x)>(&x, p);
      return x;
    }
    else{
      const auto _1 = p.pull();
      const auto _2 = p.pull();
      const auto _3 = p.pull();
      const auto _4 = p.pull();
      return static_cast<std::uint32_t>(_1 << 24 | _2 << 16 | _3 << 8 | _4);
    }
  }
  template<typename Pusher>
  static inline void write_32(Pusher& p, std::uint32_t value){
    if constexpr(std::endian::native == std::endian::big && Pusher::is_contiguous)
      push<sizeof(value)>(p, value);
    else{
      p.push((value & 0xff000000) >> 24);
      p.push((value & 0x00ff0000) >> 16);
      p.push((value & 0x0000ff00) >>  8);
      p.push( value & 0x000000ff       );
    }
  }
 private:
  template<std::uint_fast8_t Channels, typename Pusher, typename Puller>
  static inline void encode_body(Pusher& p, Puller& pixels, rgba_t (&index)[index_size], std::size_t px_len, rgba_t px_prev = {0, 0, 0, 255}, std::size_t run = 0){
    const auto f = [&run, &index, &p](rgba_t px, rgba_t px_prev){
      if(px == px_prev){
        ++run;
        return;
      }
      if(run > 0){
        while(run >= 62)[[unlikely]]{
          static constexpr std::uint8_t x = chunk_tag::run | 61;
          p.push(x);
          run -= 62;
        }
        if(run > 0){
          p.push(chunk_tag::run | (run-1));
          run = 0;
        }
      }

      const auto index_pos = px.hash() % index_size;

      if(index[index_pos] == px){
        p.push(chunk_tag::index | index_pos);
        return;
      }
      index[index_pos] = px;

      if constexpr(Channels == 4)
        if(px.a != px_prev.a){
          p.push(chunk_tag::rgba);
          push<4>(p, &px);
          return;
        }
      const auto vr = static_cast<int>(px.r) - static_cast<int>(px_prev.r) + 2;
      const auto vg = static_cast<int>(px.g) - static_cast<int>(px_prev.g) + 2;
      const auto vb = static_cast<int>(px.b) - static_cast<int>(px_prev.b) + 2;

      if(const std::uint8_t v = vr|vg|vb; v < 4){
        p.push(chunk_tag::diff | vr << 4 | vg << 2 | vb);
        return;
      }
      const auto vg_r = vr - vg + 8;
      const auto vg_b = vb - vg + 8;
      if(const int v = vg_r|vg_b, g = vg+30; ((v&0xf0)|(g&0xc0)) == 0){
        p.push(chunk_tag::luma | g);
        p.push(vg_r << 4 | vg_b);
      }
      else{
        p.push(chunk_tag::rgb);
        push<3>(p, &px);
      }
    };
    auto px = px_prev;
    while(px_len--)[[likely]]{
      px_prev = px;
      pull<Channels>(&px, pixels);
      f(px, px_prev);
    }
    if(px == px_prev){
      while(run >= 62)[[unlikely]]{
        static constexpr std::uint8_t x = chunk_tag::run | 61;
        p.push(x);
        run -= 62;
      }
      if(run > 0){
        p.push(chunk_tag::run | (run-1));
        run = 0;
      }
    }
  }
#if defined(__ARM_NEON) and not defined(QOIXX_NO_SIMD)
  template<bool Alpha>
  using pixels_type = std::conditional_t<Alpha, uint8x16x4_t, uint8x16x3_t>;
  template<bool Alpha>
  static inline pixels_type<Alpha> load(const std::uint8_t* ptr)noexcept{
    if constexpr(Alpha)
      return vld4q_u8(ptr);
    else
      return vld3q_u8(ptr);
  }
  static constexpr std::size_t simd_lanes = 16;
  template<std::uint_fast8_t Channels, typename Pusher, typename Puller>
  static inline void encode_neon(Pusher& p_, Puller& pixels_, const desc& desc){
    static constexpr bool Alpha = Channels == 4;
    write_32(p_, magic);
    write_32(p_, desc.width);
    write_32(p_, desc.height);
    p_.push(Channels);
    p_.push(static_cast<std::uint8_t>(desc.colorspace));

    std::uint8_t* p = p_.raw_pointer();
    const std::uint8_t* pixels = pixels_.raw_pointer();

    rgba_t index[index_size] = {};

    const auto zero = vdupq_n_u8(0);
    static constexpr std::uint8_t iota_[simd_lanes] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const auto iota = vld1q_u8(iota_);

    pixels_type<Alpha> prev;
    prev.val[0] = prev.val[1] = prev.val[2] = zero;
    if constexpr(Alpha)
      prev.val[3] = vdupq_n_u8(255);

    std::size_t run = 0;
    rgba_t px = {0, 0, 0, 255};

    std::size_t px_len = desc.width * desc.height;
    std::size_t simd_len = px_len / simd_lanes;
    const std::size_t simd_len_16 = simd_len * simd_lanes;
    px_len -= simd_len_16;
    pixels_.advance(simd_len_16*Channels);
    while(simd_len--){
      const auto pxs = load<Alpha>(pixels);
      pixels_type<Alpha> diff;
      diff.val[0] = vsubq_u8(pxs.val[0], vextq_u8(prev.val[0], pxs.val[0], simd_lanes-1));
      diff.val[1] = vsubq_u8(pxs.val[1], vextq_u8(prev.val[1], pxs.val[1], simd_lanes-1));
      diff.val[2] = vsubq_u8(pxs.val[2], vextq_u8(prev.val[2], pxs.val[2], simd_lanes-1));
      bool alpha = true;
      if constexpr(Alpha){
        diff.val[3] = vsubq_u8(pxs.val[3], vextq_u8(prev.val[3], pxs.val[3], simd_lanes-1));
        diff.val[3] = vceqq_u8(diff.val[3], zero);
        alpha = vminvq_u8(diff.val[3]) != 0;
      }
      auto runv = vceqq_u8(vorrq_u8(vorrq_u8(diff.val[0], diff.val[1]), diff.val[2]), zero);
      if(vminvq_u8(runv) != 0 && alpha){
        run += simd_lanes;
        pixels += simd_lanes*Channels;
        continue;
      }
      if constexpr(Alpha)
        runv = vandq_u8(runv, diff.val[3]);
      const auto r = vminvq_u8(vorrq_u8(vandq_u8(vmvnq_u8(runv), iota), runv));
      run += r;
      pixels += r*Channels;
      if(run > 0){
        while(run >= 62)[[unlikely]]{
          static constexpr std::uint8_t x = chunk_tag::run | 61;
          *p++ = x;
          run -= 62;
        }
        if(run > 0){
          *p++ = chunk_tag::run | (run-1);
          run = 0;
        }
      }
      const auto two = vdupq_n_u8(2);
      diff.val[0] = vaddq_u8(diff.val[0], two);
      diff.val[1] = vaddq_u8(diff.val[1], two);
      diff.val[2] = vaddq_u8(diff.val[2], two);
      const auto four = vdupq_n_u8(4);
      const auto diffv = vandq_u8(vorrq_u8(vorrq_u8(vdupq_n_u8(chunk_tag::diff), vshlq_n_u8(diff.val[0], 4)), vorrq_u8(vshlq_n_u8(diff.val[1], 2), diff.val[2])), vcltq_u8(vorrq_u8(vorrq_u8(diff.val[0], diff.val[1]), diff.val[2]), four));
      const auto eight = vdupq_n_u8(8);
      diff.val[0] = vaddq_u8(vsubq_u8(diff.val[0], diff.val[1]), eight);
      diff.val[2] = vaddq_u8(vsubq_u8(diff.val[2], diff.val[1]), eight);
      diff.val[1] = vaddq_u8(diff.val[1], vdupq_n_u8(30));
      const auto lu = vandq_u8(vorrq_u8(vdupq_n_u8(chunk_tag::luma), diff.val[1]), vceqq_u8(vorrq_u8(vandq_u8(vorrq_u8(diff.val[0], diff.val[2]), vdupq_n_u8(0xf0)), vandq_u8(diff.val[1], vdupq_n_u8(0xc0))), zero));
      const auto ma = vorrq_u8(vshlq_n_u8(diff.val[0], 4), diff.val[2]);
      uint8x16_t hash;
      if constexpr(Alpha)
        hash = vandq_u8(vaddq_u8(vaddq_u8(vmulq_u8(pxs.val[0], vdupq_n_u8(3)), vmulq_u8(pxs.val[1], vdupq_n_u8(5))), vaddq_u8(vmulq_u8(pxs.val[2], vdupq_n_u8(7)), vmulq_u8(pxs.val[3], vdupq_n_u8(11)))), vdupq_n_u8(63));
      else
        hash = vandq_u8(vaddq_u8(vaddq_u8(vmulq_u8(pxs.val[0], vdupq_n_u8(3)), vmulq_u8(pxs.val[1], vdupq_n_u8(5))), vaddq_u8(vmulq_u8(pxs.val[2], vdupq_n_u8(7)), vdupq_n_u8(static_cast<std::uint8_t>(255*11)))), vdupq_n_u8(63));
      std::uint8_t runs[simd_lanes], diffs[simd_lanes], lus[simd_lanes], mas[simd_lanes], hashs[simd_lanes];
      [[maybe_unused]] std::uint8_t alphas[simd_lanes];
      vst1q_u8(runs, runv);
      vst1q_u8(diffs, diffv);
      vst1q_u8(lus, lu);
      vst1q_u8(mas, ma);
      vst1q_u8(hashs, hash);
      if constexpr(Alpha)
        if(!alpha)
          vst1q_u8(alphas, diff.val[3]);
      for(std::size_t i = r; i < simd_lanes; ++i){
        if(runs[i]){
          ++run;
          pixels += Channels;
          continue;
        }
        if(run > 0){
          *p++ = chunk_tag::run | (run-1);
          run = 0;
        }
        const auto index_pos = hashs[i];
        efficient_memcpy<Channels>(&px, pixels);
        pixels += Channels;
        if(index[index_pos] == px){
          *p++ = chunk_tag::index | index_pos;
          continue;
        }
        index[index_pos] = px;

        if constexpr(Alpha)
          if(!alpha && !alphas[i]){
            *p++ = chunk_tag::rgba;
            std::memcpy(p, &px, 4);
            p += 4;
            continue;
          }
        if(diffs[i])
          *p++ = diffs[i];
        else if(lus[i]){
          *p++ = lus[i];
          *p++ = mas[i];
        }
        else{
          *p++ = chunk_tag::rgb;
          efficient_memcpy<3>(p, &px);
          p += 3;
        }
      }
      prev = pxs;
    }
    p_.advance(p-p_.raw_pointer());

    encode_body<Channels>(p_, pixels_, index, px_len, px, run);

    push<sizeof(padding)>(p_, padding);
  }
#elif defined(__AVX2__) and not defined(QOIXX_NO_SIMD)
  static constexpr unsigned de_bruijn_bit_position_sequence[32] = {
    0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8, 31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
  };
  static constexpr unsigned lsb32(std::uint32_t x)noexcept{
    return de_bruijn_bit_position_sequence[(static_cast<std::uint32_t>(x&-static_cast<std::int32_t>(x))*0x077cb531u) >> 27];
  }
  template<std::uint8_t M>
  static inline __m256i slli_epi8(__m256i v)noexcept{
    const auto mask = _mm256_set1_epi8(static_cast<std::uint8_t>(0xff << M) >> M);
    return _mm256_slli_epi16(_mm256_and_si256(v, mask), M);
  }
  template<std::uint8_t M>
  static inline __m256i mul_epi8(__m256i v)noexcept{
    if constexpr(M == 0)
      return _mm256_setzero_si256();
    else if constexpr(M == 1)
      return v;
    else if constexpr(M == 2)
      return slli_epi8<1>(v);
    else if constexpr(M == 3)
      return _mm256_add_epi8(slli_epi8<1>(v), v);
    else if constexpr(M == 4)
      return slli_epi8<2>(v);
    else if constexpr(M == 5)
      return _mm256_add_epi8(slli_epi8<2>(v), v);
    else if constexpr(M == 6)
      return _mm256_add_epi8(slli_epi8<2>(v), slli_epi8<1>(v));
    else if constexpr(M == 7)
      return _mm256_sub_epi8(slli_epi8<3>(v), v);
    else if constexpr(M == 8)
      return slli_epi8<3>(v);
    else if constexpr(M == 9)
      return _mm256_add_epi8(slli_epi8<3>(v), v);
    else if constexpr(M == 10)
      return _mm256_add_epi8(slli_epi8<3>(v), slli_epi8<1>(v));
    else if constexpr(M == 11)
      return _mm256_add_epi8(_mm256_add_epi8(slli_epi8<3>(v), slli_epi8<1>(v)), v);
    else if constexpr(M == 12)
      return _mm256_add_epi8(slli_epi8<3>(v), slli_epi8<2>(v));
    else if constexpr(M == 13)
      return _mm256_add_epi8(_mm256_add_epi8(slli_epi8<3>(v), slli_epi8<2>(v)), v);
    else if constexpr(M == 14)
      return _mm256_sub_epi8(slli_epi8<4>(v), slli_epi8<1>(v));
    else if constexpr(M == 15)
      return _mm256_sub_epi8(slli_epi8<4>(v), v);
    else
      static_assert(M <= 15);
  }
  static inline __m256i prev_vector(__m256i pxs, __m256i prev)noexcept{
    const auto permute = _mm256_permute2x128_si256(pxs, pxs, 0x08);
    const auto inserted = _mm256_inserti128_si256(permute, _mm256_extracti128_si256(prev, 1), 0);
    return _mm256_alignr_epi8(pxs, inserted, 15);
  }
  template<bool Alpha>
  struct pixels_type{
    __m256i val[3+Alpha];
  };
  static constexpr std::size_t simd_lanes = 256/8;
  template<bool Alpha>
  static inline pixels_type<Alpha> load(const std::uint8_t* ptr)noexcept{
    if constexpr(Alpha){
      const auto t1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr));
      const auto t2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr+simd_lanes));
      const auto t3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr+simd_lanes*2));
      const auto t4 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr+simd_lanes*3));
      const auto lo12 = _mm256_unpacklo_epi8(t1, t2);
      const auto lo34 = _mm256_unpacklo_epi8(t3, t4);
      const auto lolo12lo34 = _mm256_unpacklo_epi16(lo12, lo34);
      const auto hilo12lo34 = _mm256_unpackhi_epi16(lo12, lo34);
      const auto lololo12lo34hilo12lo34 = _mm256_unpacklo_epi32(lolo12lo34, hilo12lo34);
      const auto hilolo12lo34hilo12lo34 = _mm256_unpackhi_epi32(lolo12lo34, hilo12lo34);
      const auto hi12 = _mm256_unpackhi_epi8(t1, t2);
      const auto hi34 = _mm256_unpackhi_epi8(t3, t4);
      const auto lohi12hi34 = _mm256_unpacklo_epi16(hi12, hi34);
      const auto hihi12hi34 = _mm256_unpackhi_epi16(hi12, hi34);
      const auto lolohi12hi34hihi12hi34 = _mm256_unpacklo_epi32(lohi12hi34, hihi12hi34);
      const auto lolololo12lo34hilo12lo34lolohi12hi34hihi12hi34 = _mm256_unpacklo_epi64(lololo12lo34hilo12lo34, lolohi12hi34hihi12hi34);
      const auto hilololo12lo34hilo12lo34lolohi12hi34hihi12hi34 = _mm256_unpackhi_epi64(lololo12lo34hilo12lo34, lolohi12hi34hihi12hi34);
      const auto hilohi12hi34hihi12hi34 = _mm256_unpackhi_epi32(lohi12hi34, hihi12hi34);
      const auto lohilolo12lo34hilo12lo34hilohi12hi34hihi12hi34 = _mm256_unpacklo_epi64(hilolo12lo34hilo12lo34, hilohi12hi34hihi12hi34);
      const auto hihilolo12lo34hilo12lo34hilohi12hi34hihi12hi34 = _mm256_unpackhi_epi64(hilolo12lo34hilo12lo34, hilohi12hi34hihi12hi34);
      const auto mask1 = _mm256_setr_epi8(0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15, 0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15);
      const auto mask2 = _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7);
      const auto r = _mm256_permutevar8x32_epi32(_mm256_shuffle_epi8(lolololo12lo34hilo12lo34lolohi12hi34hihi12hi34, mask1), mask2);
      const auto g = _mm256_permutevar8x32_epi32(_mm256_shuffle_epi8(hilololo12lo34hilo12lo34lolohi12hi34hihi12hi34, mask1), mask2);
      const auto b = _mm256_permutevar8x32_epi32(_mm256_shuffle_epi8(lohilolo12lo34hilo12lo34hilohi12hi34hihi12hi34, mask1), mask2);
      const auto a = _mm256_permutevar8x32_epi32(_mm256_shuffle_epi8(hihilolo12lo34hilo12lo34hilohi12hi34hihi12hi34, mask1), mask2);
      return {{r, g, b, a}};
    }
    else{
      const auto t1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr));
      const auto t2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr+simd_lanes/2));
      const auto t3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr+simd_lanes));
      const auto t4 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr+simd_lanes*3/2));
      const auto t5 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr+simd_lanes*2));
      const auto t6 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr+simd_lanes*5/2));
      const auto mask01 = _mm_setr_epi8(0, 3, 6, 9, 12, 15, 1, 4, 7, 10, 13, 2, 5, 8, 11, 14);
      const auto mask02 = _mm_setr_epi8(2, 5, 8, 11, 14, 0, 3, 6, 9, 12, 15, 1, 4, 7, 10, 13);
      const auto mask03 = _mm_setr_epi8(1, 4, 7, 10, 13, 2, 5, 8, 11, 14, 0, 3, 6, 9, 12, 15);
      const auto mask11 = _mm_setr_epi8(0, 0, 0, 0, 0, 0, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128);
      const auto mask21 = _mm_setr_epi8(128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 0, 0, 0, 0, 0);
      const auto mask12 = _mm_setr_epi8(128, 128, 128, 128, 128, 0, 0, 0, 0, 0, 0, 128, 128, 128, 128, 128);
      const auto mask22 = _mm_setr_epi8(0, 0, 0, 0, 0, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128);
      const auto mask13 = _mm_setr_epi8(128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 0, 0, 0, 0, 0, 0);
      const auto mask23 = _mm_setr_epi8(128, 128, 128, 128, 128, 0, 0, 0, 0, 0, 128, 128, 128, 128, 128, 128);
      const auto x1 = _mm_shuffle_epi8(t1, mask01);
      const auto x2 = _mm_shuffle_epi8(t2, mask02);
      const auto x3 = _mm_shuffle_epi8(t3, mask03);
      const auto x4 = _mm_shuffle_epi8(t4, mask01);
      const auto x5 = _mm_shuffle_epi8(t5, mask02);
      const auto x6 = _mm_shuffle_epi8(t6, mask03);
      const auto r1 = _mm_blendv_epi8(_mm_alignr_epi8(x3, x3, 5), _mm_blendv_epi8(x1, _mm_alignr_epi8(x2, x2, 10), mask11), mask21);
      const auto g1 = _mm_blendv_epi8(_mm_alignr_epi8(x1, x1, 6), _mm_blendv_epi8(x2, _mm_alignr_epi8(x3, x3, 10), mask12), mask22);
      const auto b1 = _mm_blendv_epi8(_mm_alignr_epi8(x2, x2, 6), _mm_blendv_epi8(x3, _mm_alignr_epi8(x1, x1, 11), mask13), mask23);
      const auto r2 = _mm_blendv_epi8(_mm_alignr_epi8(x6, x6, 5), _mm_blendv_epi8(x4, _mm_alignr_epi8(x5, x5, 10), mask11), mask21);
      const auto g2 = _mm_blendv_epi8(_mm_alignr_epi8(x4, x4, 6), _mm_blendv_epi8(x5, _mm_alignr_epi8(x6, x6, 10), mask12), mask22);
      const auto b2 = _mm_blendv_epi8(_mm_alignr_epi8(x5, x5, 6), _mm_blendv_epi8(x6, _mm_alignr_epi8(x4, x4, 11), mask13), mask23);
      const auto r = _mm256_inserti128_si256(_mm256_castsi128_si256(r1), r2, 1);
      const auto g = _mm256_inserti128_si256(_mm256_castsi128_si256(g1), g2, 1);
      const auto b = _mm256_inserti128_si256(_mm256_castsi128_si256(b1), b2, 1);
      return {{r, g, b}};
    }
  }
  template<std::uint_fast8_t Channels, typename Pusher, typename Puller>
  static inline void encode_avx2(Pusher& p_, Puller& pixels_, const desc& desc){
    static constexpr bool Alpha = Channels == 4;
    write_32(p_, magic);
    write_32(p_, desc.width);
    write_32(p_, desc.height);
    p_.push(Channels);
    p_.push(static_cast<std::uint8_t>(desc.colorspace));

    std::uint8_t* p = p_.raw_pointer();
    const std::uint8_t* pixels = pixels_.raw_pointer();

    rgba_t index[index_size] = {};

    const auto zero = _mm256_setzero_si256();

    pixels_type<Alpha> prev;
    prev.val[0] = prev.val[1] = prev.val[2] = zero;
    if constexpr(Alpha)
      prev.val[3] = _mm256_set1_epi8(255);

    std::size_t run = 0;
    rgba_t px = {0, 0, 0, 255};

    std::size_t px_len = desc.width * desc.height;
    std::size_t simd_len = px_len / simd_lanes;
    const std::size_t simd_len_32 = simd_len * simd_lanes;
    px_len -= simd_len_32;
    pixels_.advance(simd_len_32*Channels);
    while(simd_len--){
      const auto pxs = load<Alpha>(pixels);
      pixels_type<Alpha> diff;
      diff.val[0] = _mm256_sub_epi8(pxs.val[0], prev_vector(pxs.val[0], prev.val[0]));
      diff.val[1] = _mm256_sub_epi8(pxs.val[1], prev_vector(pxs.val[1], prev.val[1]));
      diff.val[2] = _mm256_sub_epi8(pxs.val[2], prev_vector(pxs.val[2], prev.val[2]));
      bool alpha = true;
      if constexpr(Alpha){
        diff.val[3] = _mm256_sub_epi8(pxs.val[3], prev_vector(pxs.val[3], prev.val[3]));
        alpha = _mm256_testz_si256(diff.val[3], diff.val[3]);
        diff.val[3] = _mm256_cmpeq_epi8(diff.val[3], zero);
      }
      const auto ored = _mm256_or_si256(_mm256_or_si256(diff.val[0], diff.val[1]), diff.val[2]);
      auto runv = _mm256_cmpeq_epi8(ored, zero);
      if(_mm256_testz_si256(ored, ored) && alpha){
        run += simd_lanes;
        pixels += simd_lanes*Channels;
        continue;
      }
      if constexpr(Alpha)
        runv = _mm256_and_si256(runv, diff.val[3]);
      const auto r = lsb32(~_mm256_movemask_epi8(runv));
      run += r;
      pixels += r*Channels;
      if(run > 0){
        while(run >= 62)[[unlikely]]{
          static constexpr std::uint8_t x = chunk_tag::run | 61;
          *p++ = x;
          run -= 62;
        }
        if(run > 0){
          *p++ = chunk_tag::run | (run-1);
          run = 0;
        }
      }
      const auto two = _mm256_set1_epi8(2);
      diff.val[0] = _mm256_add_epi8(diff.val[0], two);
      diff.val[1] = _mm256_add_epi8(diff.val[1], two);
      diff.val[2] = _mm256_add_epi8(diff.val[2], two);
      const auto diffor = _mm256_or_si256(_mm256_or_si256(diff.val[0], diff.val[1]), diff.val[2]);
      const auto diffv = _mm256_and_si256(_mm256_or_si256(_mm256_or_si256(_mm256_set1_epi8(chunk_tag::diff), slli_epi8<4>(diff.val[0])), _mm256_or_si256(slli_epi8<2>(diff.val[1]), diff.val[2])), _mm256_cmpeq_epi8(_mm256_and_si256(diffor, _mm256_set1_epi8(0b11)), diffor));
      const auto eight = _mm256_set1_epi8(8);
      diff.val[0] = _mm256_add_epi8(_mm256_sub_epi8(diff.val[0], diff.val[1]), eight);
      diff.val[2] = _mm256_add_epi8(_mm256_sub_epi8(diff.val[2], diff.val[1]), eight);
      diff.val[1] = _mm256_add_epi8(diff.val[1], _mm256_set1_epi8(30));
      const auto lu = _mm256_and_si256(_mm256_or_si256(_mm256_set1_epi8(chunk_tag::luma), diff.val[1]), _mm256_cmpeq_epi8(_mm256_or_si256(_mm256_and_si256(_mm256_or_si256(diff.val[0], diff.val[2]), _mm256_set1_epi8(0xf0)), _mm256_and_si256(diff.val[1], _mm256_set1_epi8(0xc0))), zero));
      const auto ma = _mm256_or_si256(slli_epi8<4>(diff.val[0]), diff.val[2]);
      __m256i hash;
      if constexpr(Alpha)
        hash = _mm256_and_si256(_mm256_add_epi8(_mm256_add_epi8(mul_epi8<3>(pxs.val[0]), mul_epi8<5>(pxs.val[1])), _mm256_add_epi8(mul_epi8<7>(pxs.val[2]), mul_epi8<11>(pxs.val[3]))), _mm256_set1_epi8(63));
      else
        hash = _mm256_and_si256(_mm256_add_epi8(_mm256_add_epi8(mul_epi8<3>(pxs.val[0]), mul_epi8<5>(pxs.val[1])), _mm256_add_epi8(mul_epi8<7>(pxs.val[2]), _mm256_set1_epi8(static_cast<std::uint8_t>(255*11)))), _mm256_set1_epi8(63));
      alignas(alignof(__m256i)) std::uint8_t runs[simd_lanes], diffs[simd_lanes], lus[simd_lanes], mas[simd_lanes], hashs[simd_lanes];
      [[maybe_unused]] alignas(alignof(__m256i)) std::uint8_t alphas[simd_lanes];
      _mm256_store_si256(reinterpret_cast<__m256i*>(runs), runv);
      _mm256_store_si256(reinterpret_cast<__m256i*>(diffs), diffv);
      _mm256_store_si256(reinterpret_cast<__m256i*>(lus), lu);
      _mm256_store_si256(reinterpret_cast<__m256i*>(mas), ma);
      _mm256_store_si256(reinterpret_cast<__m256i*>(hashs), hash);
      if constexpr(Alpha)
        if(!alpha)
          _mm256_store_si256(reinterpret_cast<__m256i*>(alphas), diff.val[3]);
      for(std::size_t i = r; i < simd_lanes; ++i){
        if(runs[i]){
          ++run;
          pixels += Channels;
          continue;
        }
        if(run > 0){
          *p++ = chunk_tag::run | (run-1);
          run = 0;
        }
        const auto index_pos = hashs[i];
        efficient_memcpy<Channels>(&px, pixels);
        pixels += Channels;
        if(index[index_pos] == px){
          *p++ = chunk_tag::index | index_pos;
          continue;
        }
        index[index_pos] = px;

        if constexpr(Alpha)
          if(!alpha && !alphas[i]){
            *p++ = chunk_tag::rgba;
            std::memcpy(p, &px, 4);
            p += 4;
            continue;
          }
        if(diffs[i])
          *p++ = diffs[i];
        else if(lus[i]){
          *p++ = lus[i];
          *p++ = mas[i];
        }
        else{
          *p++ = chunk_tag::rgb;
          efficient_memcpy<3>(p, &px);
          p += 3;
        }
      }
      prev = pxs;
    }
    p_.advance(p-p_.raw_pointer());

    encode_body<Channels>(p_, pixels_, index, px_len, px, run);

    push<sizeof(padding)>(p_, padding);
  }
#endif

  template<std::uint_fast8_t Channels, typename Pusher, typename Puller>
  static inline void encode_impl(Pusher& p, Puller& pixels, const desc& desc){
    write_32(p, magic);
    write_32(p, desc.width);
    write_32(p, desc.height);
    p.push(Channels);
    p.push(static_cast<std::uint8_t>(desc.colorspace));

    rgba_t index[index_size] = {};

    std::size_t px_len = desc.width * desc.height;
    encode_body<Channels>(p, pixels, index, px_len);

    push<sizeof(padding)>(p, padding);
  }

  template<typename Puller>
  static inline desc decode_header(Puller& p){
    desc d;
    const auto magic_ = read_32(p);
    d.width = read_32(p);
    d.height = read_32(p);
    d.channels = p.pull();
    d.colorspace = static_cast<qoi::colorspace>(p.pull());
    if(
      d.width == 0 || d.height == 0 || magic_ != magic ||
      d.channels < 3 || d.channels > 4 ||
      d.height >= pixels_max / d.width
    )[[unlikely]]
      throw std::runtime_error("qoixx::qoi::decode: invalid header");
    return d;
  }

  template<std::size_t Channels, typename Pusher, typename Puller>
  static inline void decode_impl(Pusher& pixels, Puller& p, std::size_t px_len, std::size_t size){
    rgba_t px = {0, 0, 0, 255};
    rgba_t index[index_size];
    index[(0*3+0*5+0*7+0*11)%index_size] = {};
    index[(0*3+0*5+0*7+255*11)%index_size] = px;

    const auto f = [&pixels, &p, &px_len, &size, &px, &index]{
      static constexpr std::uint32_t mask_tail_6 = 0b0011'1111u;
      static constexpr std::uint32_t mask_tail_4 = 0b0000'1111u;
      static constexpr std::uint32_t mask_tail_2 = 0b0000'0011u;
      const auto b1 = p.pull();
      --size;

#if defined(__ARM_NEON) and not defined(QOIXX_NO_SIMD)
#define QOIXX_HPP_DECODE_RUN(px, run) { \
    ++run; \
    if(run >= 8){\
      std::conditional_t<Channels == 4, uint8x8x4_t, uint8x8x3_t> data = {vdup_n_u8(px.r), vdup_n_u8(px.g), vdup_n_u8(px.b)}; \
      if constexpr(Channels == 4) \
        data.val[3] = vdup_n_u8(px.a); \
      while(run>=8){ \
        if constexpr(Channels == 4) \
          vst4_u8(pixels.raw_pointer(), data); \
        else \
          vst3_u8(pixels.raw_pointer(), data); \
        pixels.advance(Channels*8); \
        run -= 8; \
      } \
    } \
    while(run--){push<Channels>(pixels, &px);} \
  }
#else
#define QOIXX_HPP_DECODE_RUN(px, run) do{push<Channels>(pixels, &px);}while(run--);
#endif

#define QOIXX_HPP_DECODE_SWITCH(...) \
      if(b1 >= chunk_tag::run){ \
        switch(b1){ \
          __VA_ARGS__ \
          case chunk_tag::rgb: \
            pull<3>(&px, p); \
            size -= 3; \
            break; \
        default: \
          /*run*/ \
          std::size_t run = b1 & mask_tail_6; \
          if(run >= px_len)[[unlikely]] \
            run = px_len; \
          px_len -= run; \
          QOIXX_HPP_DECODE_RUN(px, run) \
          return; \
        } \
      } \
      else if(b1 >= chunk_tag::luma){ \
        /*luma*/ \
        const auto b2 = p.pull(); \
        --size; \
        static constexpr int vgv = chunk_tag::luma+40; \
        const int vg = b1 - vgv; \
        px.r += vg + (b2 >> 4); \
        px.g += vg + 8; \
        px.b += vg + (b2 & mask_tail_4); \
      } \
      else if(b1 >= chunk_tag::diff){ \
        /*diff*/ \
        px.r += ((b1 >> 4) & mask_tail_2) - 2; \
        px.g += ((b1 >> 2) & mask_tail_2) - 2; \
        px.b += ( b1       & mask_tail_2) - 2; \
      } \
      else{ \
        /*index*/ \
        px = index[b1]; \
        push<Channels>(pixels, &px); \
        return; \
      }
      if constexpr(Channels == 4)
        QOIXX_HPP_DECODE_SWITCH(
          case chunk_tag::rgba:
            pull<4>(&px, p);
            size -= 4;
            break;
        )
      else
        QOIXX_HPP_DECODE_SWITCH(
          [[unlikely]] case chunk_tag::rgba:
            pull<3>(&px, p);
            p.advance(1);
            size -= 4;
            break;
        )
#undef QOIXX_HPP_DECODE_SWITCH
#undef QOIXX_HPP_DECODE_RUN
      index[px.hash() % index_size] = px;

      push<Channels>(pixels, &px);
    };

    while(px_len--){
      f();
      if(size < sizeof(padding))[[unlikely]]{
        throw std::runtime_error("qoixx::qoi::decode: insufficient input data");
      }
    }
  }
 public:
  template<typename T, typename U>
  static inline T encode(const U& u, const desc& desc){
    using coU = container_operator<U>;
    if(!coU::valid(u) || coU::size(u) < desc.width*desc.height*desc.channels || desc.width == 0 || desc.height == 0 || desc.channels < 3 || desc.channels > 4 || desc.height >= pixels_max / desc.width)[[unlikely]]
      throw std::invalid_argument{"qoixx::qoi::encode: invalid argument"};

    const auto max_size = static_cast<std::size_t>(desc.width) * desc.height * (desc.channels + 1) + header_size + sizeof(padding);
    using coT = container_operator<T>;
    T data = coT::construct(max_size);
    auto p = coT::create_pusher(data);
    auto puller = coU::create_puller(u);

#ifndef QOIXX_NO_SIMD
#if defined(__ARM_NEON)
    if constexpr(coT::pusher::is_contiguous && coU::puller::is_contiguous)
      if(desc.channels == 4)
        encode_neon<4>(p, puller, desc);
      else
        encode_neon<3>(p, puller, desc);
    else
#elif defined(__AVX2__)
    if constexpr(coT::pusher::is_contiguous && coU::puller::is_contiguous)
      if(desc.channels == 4)
        encode_avx2<4>(p, puller, desc);
      else
        encode_avx2<3>(p, puller, desc);
    else
#endif
#endif
      if(desc.channels == 4)
        encode_impl<4>(p, puller, desc);
      else
        encode_impl<3>(p, puller, desc);

    return p.finalize();
  }
  template<typename T, typename U>
  requires(sizeof(U) == 1)
  static inline T encode(const U* pixels, std::size_t size, const desc& desc){
    return encode<T>(std::make_pair(pixels, size), desc);
  }
  template<typename T, typename U>
  static inline std::pair<T, desc> decode(const U& u, std::uint8_t channels = 0){
    using coU = container_operator<U>;
    const auto size = coU::size(u);
    if(!coU::valid(u) || size < header_size + sizeof(padding) || (channels != 0 && channels != 3 && channels != 4))[[unlikely]]
      throw std::invalid_argument{"qoixx::qoi::decode: invalid argument"};
    auto puller = coU::create_puller(u);

    const auto d = decode_header(puller);
    if(channels == 0)
      channels = d.channels;

    const std::size_t px_len = static_cast<std::size_t>(d.width) * d.height;
    using coT = container_operator<T>;
    T data = coT::construct(px_len*channels);
    auto p = coT::create_pusher(data);

    if(channels == 4)
      decode_impl<4>(p, puller, px_len, size);
    else
      decode_impl<3>(p, puller, px_len, size);

    return std::make_pair(std::move(p.finalize()), d);
  }
  template<typename T, typename U>
  requires(sizeof(U) == 1)
  static inline std::pair<T, desc> decode(const U* pixels, std::size_t size, std::uint8_t channels = 0){
    return decode<T>(std::make_pair(pixels, size), channels);
  }
};

}

#endif //QOIXX_HPP_INCLUDED_
