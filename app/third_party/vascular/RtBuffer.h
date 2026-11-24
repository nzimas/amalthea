// Amalthea adapter: bounded per-voice scratch, reserved before audio starts.
#pragma once
#include <algorithm>
#include <cstddef>
#include <new>
namespace amalthea_rt {
struct Arena {
 unsigned char *data;size_t capacity,used=0;
 void *take(size_t bytes,size_t alignment){size_t at=(used+alignment-1)&~(alignment-1);if(at+bytes>capacity)throw std::bad_alloc();used=at+bytes;return data+at;}
};
inline thread_local Arena *current=nullptr;
template<class T> struct Buffer {
 T *p=nullptr;size_t n=0,capacity=0;
 void assign(size_t size,T value){if(size>capacity){p=static_cast<T*>(current->take(size*sizeof(T),alignof(T)));capacity=size;}n=size;std::fill(p,p+n,value);}
 void clear(){n=0;}
 bool empty()const{return n==0;}size_t size()const{return n;}
 T &operator[](size_t i){return p[i];}const T &operator[](size_t i)const{return p[i];}
 T *begin(){return p;}T *end(){return p+n;}
};
}
