#pragma once
#include <string.h>
#include <unordered_map>
#ifndef _WIN32
#include <sys/mman.h>
#endif

// It appears that on OSX MAP_ANONYMOUS is mapped to MAP_ANON
// https://github.com/leftmike/foment/issues/4
#ifdef __APPLE__
#define MAP_ANONYMOUS MAP_ANON
#endif

typedef float weight;

class weight_parameters;
class sparse_weight_parameters;
typedef std::unordered_map<size_t, weight*> weight_map;

template <typename T> 
class weights_iterator_iterator
{
private:
	T* _cur;
public:
	weights_iterator_iterator(T* cur)
		: _cur(cur)
	{ }
	
	T& operator*() { return *_cur; }

	weights_iterator_iterator& operator++()
	{
		++_cur;
		return *this;
	}

	weights_iterator_iterator operator+(size_t index) { return weights_iterator_iterator(_cur + index); }

	weights_iterator_iterator& operator+=(size_t index)
	{
		_cur += index;
		return *this;
	}

	bool operator==(const weights_iterator_iterator& rhs) const { return _cur == rhs._cur; }
	bool operator!=(const weights_iterator_iterator& rhs) const { return _cur != rhs._cur; }

};

template <typename T>
class weights_iterator
{
private:
	T* _current;
	size_t _idx;
	uint32_t _stride;

public:
	typedef std::forward_iterator_tag iterator_category;
	typedef T value_type;
	typedef ptrdiff_t difference_type;
	typedef  T* pointer;
	typedef  T& reference;

	typedef weights_iterator_iterator<T> w_iter;
	
	weights_iterator(T* current, size_t idx, uint32_t stride )
		: _current(current), _idx(idx), _stride(stride)
	{ }

	T& operator*() { return *_current; }

	uint64_t operator-(const weights_iterator& rhs) { return _current - rhs._current;}

	size_t index() { return _idx; }
	
	weights_iterator& operator++()
	{
		_current += _stride;
		++_idx;
		return *this;
	}

	bool operator==(const weights_iterator& rhs) const { return _current == rhs._current; }
	bool operator!=(const weights_iterator& rhs) const { return _current != rhs._current; }

	//to iterate within a bucket
	w_iter begin() { return w_iter(_current); }
	w_iter end() { return w_iter(_current + _stride); }
	w_iter end(size_t offset) { return w_iter(_current + offset); }
};

class weight_parameters 
{
private:
	weight* _begin;
	uint64_t _weight_mask;  // (stride*(1 << num_bits) -1)
	uint32_t _stride_shift;
	uint32_t _stride;
	bool _seeded; // whether the instance is sharing model state with others

public:
	typedef weights_iterator<weight> iterator;
	typedef weights_iterator<const weight> const_iterator;
	void* set_struct;
	weight_parameters(size_t length, uint32_t stride_shift=0)
		: _begin(calloc_mergable_or_throw<weight>(length << stride_shift)),
		_weight_mask((length << stride_shift) - 1),	
		_stride_shift(stride_shift),
		_stride(1<< stride_shift),
		_seeded(false)
		{ }

 weight_parameters()
	 : _begin(nullptr), _weight_mask(0), _stride_shift(0), _stride(1), _seeded(false)
	  {}
	
	bool not_null() { return (_weight_mask > 0 && _begin != nullptr);}

	weight_parameters(const weight_parameters &other) { shallow_copy(other); }
	weight_parameters(weight_parameters &&) = delete;

	weight* first() { return _begin; } //TODO: Temporary fix for allreduce.
	
	//iterator with stride 
	iterator begin() { return iterator(_begin, 0, _stride); }
	iterator end() { return iterator(_begin + _weight_mask + 1, floor((_weight_mask + 1) / _stride), _stride); }

	//const iterator
	const_iterator cbegin() { return const_iterator(_begin, 0,  _stride); }
	const_iterator cend() { return const_iterator(_begin + _weight_mask + 1, floor((_weight_mask + 1)/_stride), _stride); }

	inline weight& operator[](size_t i) const { return _begin[i & _weight_mask]; }
	void shallow_copy(const weight_parameters& input)
	{ _begin = input._begin;
	  _weight_mask = input._weight_mask;
	  _stride_shift = input._stride_shift;
	  _seeded = true;
	}

	inline weight& strided_index(size_t index){	return _begin[index << _stride_shift];}

	template<void(*T)(iterator&, uint64_t, uint32_t, void*)>  
	inline void set_default()
	{ 
	iterator iter = begin();
	  for (size_t i = 0; iter != end(); ++iter, i += _stride)
			T(iter, i, _stride, set_struct);
	}

	void set_zero(size_t offset)
	{
		for (iterator iter = begin(); iter != end(); ++iter)
			(&(*iter))[offset] = 0;
	}
	
	uint64_t mask()
	{ return _weight_mask;
	}

	uint64_t seeded()
	{  return _seeded;
	}

	uint32_t stride_shift()
	{ return _stride_shift;		
	}		
	
	void stride_shift(uint32_t stride_shift)		
	{ _stride_shift = stride_shift;		
	}
	
	#ifndef _WIN32
	void share(size_t length)
	{
	  float* shared_weights = (float*)mmap(0, (length << _stride_shift) * sizeof(float),
			                  PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
          size_t float_count = length << _stride_shift;
      	  weight* dest = shared_weights;
		  memcpy(dest, _begin, float_count*sizeof(float));
      	  free(_begin);
      	  _begin = dest;
	}
	#endif
	
	~weight_parameters()
	{  if (_begin != nullptr && !_seeded)  // don't free weight vector if it is shared with another instance
	   {  free(_begin);
	      _begin = nullptr;
	   }
	}
};

template <typename T>
class sparse_weights_iterator
{
private:
	weight_map::iterator& _iter;
	uint32_t _stride;

public:
	typedef std::forward_iterator_tag iterator_category;
	typedef T value_type;
	typedef ptrdiff_t difference_type;
	typedef  T* pointer;
	typedef  T& reference;

	typedef weights_iterator_iterator<T> w_iter;

	sparse_weights_iterator(weight_map::iterator& iter, uint32_t stride)
		: _iter(iter), _stride(stride)
	{ }

	sparse_weights_iterator& operator=(const sparse_weights_iterator& other)
	{
		_iter = other._iter;
		_stride = other._stride;
		return *this;

	}
	size_t index() { return _iter->first; }

	T& operator*() { 
		return *(_iter->second);
	} 

	uint64_t operator-(const sparse_weights_iterator& rhs) { return _iter->first - rhs._iter->first;}

	sparse_weights_iterator& operator++()
	{  
		_iter++;
		return *this;
	}

	bool operator==(const sparse_weights_iterator& rhs) const { return _iter == rhs._iter; }
	bool operator!=(const sparse_weights_iterator& rhs) const { return _iter != rhs._iter; }

	//to iterate within a bucket
	w_iter begin() { return w_iter(_iter->second);}
	w_iter end() { return w_iter(_iter->second + _stride); }
	w_iter end(size_t offset) { return w_iter(_iter->second + offset);}
};


class sparse_weight_parameters
{
private:
	weight_map _map;
	uint64_t _weight_mask;  // (stride*(1 << num_bits) -1)
	uint32_t _stride_shift;
	uint32_t _stride;
	bool _seeded; // whether the instance is sharing model state with others
	bool _delete;

public:
	typedef sparse_weights_iterator<weight> iterator;
	typedef sparse_weights_iterator<const weight> const_iterator;
	void(*fun)(iterator&, uint64_t, uint32_t, void*);
	void* set_struct;

	sparse_weight_parameters(size_t length, uint32_t stride_shift = 0)
		: _map(),
		_weight_mask((length << stride_shift) - 1),
		_stride_shift(stride_shift),
		_stride(1 << stride_shift),
		_seeded(false), _delete(false),
		fun(nullptr)
	{}

	sparse_weight_parameters()
		: _map(), _weight_mask(0), _stride_shift(0), _stride(1), _seeded(false), _delete(false), fun(nullptr)
	{}

	bool not_null() { return (_weight_mask > 0 && !_map.empty()); }

	sparse_weight_parameters(const sparse_weight_parameters &other) { shallow_copy(other); }
	sparse_weight_parameters(sparse_weight_parameters &&) = delete;

	weight* first() { throw 1; } //TODO: Throw better exceptions. Allreduce currently not supported in sparse.

	//iterator with stride 
	iterator begin() { weight_map::iterator i = _map.begin(); return iterator(i, _stride); }
	iterator end() { weight_map::iterator i = _map.end(); return iterator(i, _stride); }

	//const iterator
	const_iterator cbegin() { weight_map::iterator i = _map.begin(); return const_iterator(i,  _stride); }
	const_iterator cend() { weight_map::iterator i = _map.begin(); return const_iterator(i, _stride); }
	
	inline weight& operator[](size_t i)
	{   uint64_t index = floor((i & _weight_mask)/ _stride);
		weight_map::iterator iter = _map.find(index);
		weight_map::iterator end = _map.end();
		if (iter == end) 
		{   _map.insert(std::make_pair(index, calloc_mergable_or_throw<weight>(_stride)));
			iter = _map.find(index);
			if (fun != nullptr)
			  {
			    iterator i(iter,_stride);
			    fun(i, index << _stride_shift, _stride, set_struct);
			  }
			iter = _map.find(index);
		}
		weight* it = iter->second;
		size_t offset = (i & _weight_mask) % _stride;
		return (&(*it))[offset];

	}

	weight& strided_index(size_t index)
	{
		weight_map::iterator iter = _map.find(index);
		weight_map::iterator end = _map.end();
		if (iter == end)
		{  _map.insert(std::make_pair(index, calloc_mergable_or_throw<weight>(_stride)));
		   iter = _map.find(index);
		   if (fun != nullptr)
		     {
		       iterator i(iter,_stride);
		       fun(i, index << _stride_shift, _stride, set_struct);
		     }
		}	
		weight* it = iter->second;
		return *it;
	}
	
	void shallow_copy(const sparse_weight_parameters& input)
	{
		_map = input._map;
		_weight_mask = input._weight_mask;
		_stride_shift = input._stride_shift;
		_seeded = true;
	}

	template<void(*T)(iterator&, uint64_t, uint32_t, void*)> //for random initialization of the entire weight_vector 
	inline void set_default()
	{
		fun = T;
	}

	void set_zero(size_t offset)
	{
		for (weight_map::iterator iter = _map.begin(); iter != _map.end(); ++iter){
			(&(*(iter->second)))[offset] = 0;
		}
	}

	uint64_t mask()
	{
		return _weight_mask;
	}

	uint64_t seeded()
	{
		return _seeded;
	}

	uint32_t stride_shift()
	{
		return _stride_shift;
	}

	void stride_shift(uint32_t stride_shift)
	{
		_stride_shift = stride_shift;
	}

#ifndef _WIN32
	void share(size_t length)
	{throw 1; //TODO: add better exceptions
	}
#endif

	~sparse_weight_parameters()
	{if (!_delete && !_seeded)  // don't free weight vector if it is shared with another instance
		{
		 _map.clear();
		 _delete = true;
		}
	}
};




