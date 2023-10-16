// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <string>
#include <vector>
#include "ref_counted.h"
#include "types.h"

class FileMapping : public ref_counted<FileMapping> {
    void *mapping;
    size_t num_bytes;
#ifdef _WIN32
    void* file_handle;
    void* mapping_handle;
#else
    int file;
#endif

    template <class D> friend struct ref_counted;
    void release_resources();
public:
    FileMapping(const std::string &fname);
    FileMapping(std::nullptr_t) : ref_counted(nullptr) { }
    ~FileMapping();

    const uint8_t *data() const;
    size_t nbytes() const;
};

// untyped buffer reference storing std::vector<T> data
class GenericBuffer : public ref_counted<GenericBuffer> {
    template <class D> friend struct ref_counted;

protected:
    struct shared_data {
        virtual ~shared_data() = default;
        virtual void clear() = 0;
        virtual uint8_t *data() = 0;
        virtual const uint8_t *data() const = 0;
        virtual size_t nbytes() const = 0;
    };
    template <class T>
    struct typed_shared_data : ref_counted::ref_counted_data {
        std::vector<T> store;
        typed_shared_data(size_t size) : store(size) { }
        typed_shared_data(std::vector<T>&& v) : store((std::vector<T>&&) v) { }
        typed_shared_data(std::vector<T> const& v) : store(v) { }
        void clear() override { store.clear(); }
        uint8_t *data() override { return (uint8_t*) store.data(); }
        const uint8_t *data() const override { return (const uint8_t*) store.data(); }
        size_t nbytes() const override { return sizeof(*store.data()) * store.size(); }
    };

public:
    template <class T> struct make_type { };
    template <class T> GenericBuffer(make_type<T>, size_t size = 0) : ref_counted(new typed_shared_data<T>(size)) { }
    template <class T> GenericBuffer(std::vector<T>&& v) : ref_counted(new typed_shared_data<T>((std::vector<T>&&) v)) { }
    template <class T> GenericBuffer(std::vector<T> const& v) : ref_counted(new typed_shared_data<T>(v)) { }
    GenericBuffer(std::nullptr_t) : ref_counted(nullptr) { }
    void release_resources() { if (this->ref_data) this->ref_data->clear(); }
    ~GenericBuffer() { this->discard_reference(); }

    uint8_t* data() { return (this->ref_data) ? this->ref_data->data() : nullptr; }
    const uint8_t* data() const { return (this->ref_data) ? this->ref_data->data() : nullptr; }
    size_t nbytes() const { return (this->ref_data) ? this->ref_data->nbytes() : 0; }

    // note: these methods require that the same internal buffer is always accessed with the same type!
    template <class T>
    std::vector<T>& get_vector() { return ((typed_shared_data<T>*) this->ref_data)->store; }
    template <class T>
    const std::vector<T>& get_vector() const { return ((typed_shared_data<T>*) this->ref_data)->store; }

    template <class T>
    std::vector<T>& to_vector() {
        if (!this->ref_data)
            *this = GenericBuffer(make_type<T>());
        return ((typed_shared_data<T>*) this->ref_data)->store;
    }
};
// typed buffer reference storing std::vector<T> data
template <class T>
class Buffer : public GenericBuffer {
public:
    explicit Buffer(size_t size) : GenericBuffer(make_type<T>(), size) { }
    Buffer(make_type<T>, size_t size) : GenericBuffer(make_type<T>(), size) { }
    Buffer(std::vector<T>&& v) : GenericBuffer((std::vector<T>&&) v) { }
    Buffer(std::vector<T> const& v) : GenericBuffer(v) { }
    Buffer(std::nullptr_t) : GenericBuffer(nullptr) { }

    std::vector<T>& get_vector() { return ((typed_shared_data<T>*) this->ref_data)->store; }
    const std::vector<T>& get_vector() const { return ((typed_shared_data<T>*) this->ref_data)->store; }

    std::vector<T>& to_vector() {
        if (!this->ref_data)
            *this = Buffer(size_t(0));
        return ((typed_shared_data<T>*) this->ref_data)->store;
    }
};

template <class T>
struct buffer_t {
    typedef Buffer<T> type;
};
template <>
struct buffer_t<void> {
    typedef GenericBuffer type;
};

template <class T>
class mapped_vector {
public:
    typedef typename buffer_t<T>::type buffer_type;
    typedef T value_type;
private:
    union data_store {
        buffer_type buffer;
        FileMapping mapping;
        ~data_store() = delete;
    };
    char store[sizeof(data_store)];
    ptrdiff_t map_offset = -1;
    size_t map_size = 0;

public:
    mapped_vector(buffer_type buffer = nullptr, size_t offset = 0, size_t size = (size_t) -1)
        : map_offset(-1 - (ptrdiff_t) offset)
        , map_size(size) {
        new (store) buffer_type(buffer);
    }
    mapped_vector(FileMapping mapping, size_t offset = 0, size_t size = (size_t) -1)
        : map_offset(offset)
        , map_size(size) {
        new (store) FileMapping(mapping);
    }
    mapped_vector(mapped_vector const& right) noexcept
        : map_offset(right.map_offset)
        , map_size(right.map_size) {
        if (map_offset < 0)
            new (store) buffer_type((buffer_type const&) right.store);
        else
            new (store) FileMapping((FileMapping const&) right.store);
    }
    mapped_vector& operator =(mapped_vector const& right) noexcept {
        if (this != &right) {
            this->~mapped_vector();
            new (this) mapped_vector(right);
        }
        return *this;
    }
    ~mapped_vector() {
        if (map_offset < 0)
            ((buffer_type&) store).~buffer_type();
        else
            ((FileMapping&) store).~FileMapping();
    }

    buffer_type* buffer() { return (map_offset < 0) ? &(buffer_type&) store : nullptr; }
    buffer_type const* buffer() const { return (map_offset < 0) ? &(buffer_type const&) store : nullptr; }
    FileMapping* mapping() { return (map_offset >= 0) ? &(FileMapping&) store : nullptr; }
    FileMapping const* mapping() const { return (map_offset >= 0) ? &(FileMapping const&) store : nullptr; }

    // note: this method requires that the same internal buffer is always accessed with the same type!
    template <class T2 = T>
    std::vector<T2>& make_vector(size_t size = 0) {
        T* type_sanity_check = (T2*) 0;
        (void) type_sanity_check;
        *this = buffer_type(typename buffer_type::template make_type<T2>(), size);
        return static_cast<GenericBuffer&>((buffer_type&) store).template get_vector<T2>();
    }

    uint8_t const* bytes() const {
        if (map_offset < 0)
            return ((buffer_type const&) store).data() + (-1 - map_offset);
        else
            return ((FileMapping const&) store).data() + map_offset;
    }

    size_t offset() const { return map_offset >= 0 ? map_offset : -1 - map_offset; }
    size_t nbytes() const {
        if (map_size == (size_t) -1) {
            if (map_offset < 0)
                return ((buffer_type const&) store).nbytes();
            else
                return ((FileMapping const&) store).nbytes();
        }
        else
            return map_size;
    }

    void set_offset(size_t offset) {
        if (map_offset < 0)
            map_offset = -1 - (ptrdiff_t) offset;
        else
            map_offset = offset;
    }
    void set_nbytes(size_t size = (size_t) -1) {
        map_size = size;
    }

    T const* data() const { return (T const*) bytes(); }
    size_t size() const { return nbytes() / sizeof((T const&) store); } // trigger error with void
    template <class T2 = T>
    size_t count() const { T* type_sanity_check = (T2*) 0; (void) type_sanity_check; return nbytes() / sizeof(T2); }
    bool empty() const { return nbytes() == 0; }

    T const* begin() const { return (T const*) bytes(); }
    T const* end() const { return (T const*) bytes() + nbytes() / sizeof(T); }

    template <class T2>
    mapped_range<T2 const> as_range() const { T* type_sanity_check = (T2*) 0; (void) type_sanity_check; return { (T2 const*) bytes(), (T2 const*) bytes() + nbytes() / sizeof(T2) }; }
};
