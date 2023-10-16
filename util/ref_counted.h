// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <cassert>

bool in_stack_unwind();

template <class D>
struct ref_counted {
    struct shared_data { }; // no data shared by default
    
    struct ref_counted_data;
    ref_counted_data* ref_data;

    ref_counted()
        : ref_data(new ref_counted_data()) {
    }
    ref_counted(ref_counted const& right) noexcept
        : ref_data(right.ref_data) {
        if (ref_data)
            ++ref_data->ref_count;
    }
    ref_counted& operator =(ref_counted const& right) noexcept {
        if (ref_data == right.ref_data)
            return *this;
        if (ref_data && --ref_data->ref_count == 0) {
            static_cast<D*>(this)->release_resources();
            delete ref_data;
        }
        ref_data = right.ref_data;
        if (ref_data)
            ++ref_data->ref_count;
        return *this;
    }

    typedef void (D::*release_resources_t)();
    // always call this function from the destructor of any derived classes! Thus,
    // all subobjects entering release_resources() are still in fully constructed state.
    // note: this declaration is made easy to allow overriding+redirection for purposes
    // of hiding private data declarations in resp. translation unit
    void discard_reference(release_resources_t release_resources = nullptr) noexcept {
        if (ref_data && --ref_data->ref_count == 0) {
            if (!release_resources)
                static_cast<D*>(this)->release_resources();
            else
                (static_cast<D*>(this)->*release_resources)();
            delete ref_data;
        }
        // object may only be destructed after this (repeated discard_reference() is OK)
        ref_data = nullptr;
    }
    template <class DerivedT>
    void discard_reference() noexcept {
        // allow ref-counted classes to move discard_reference into a translation unit,
        // hiding the private definition of shared_data. Call override in this case:
        static_cast<D*>(this)->discard_reference(
                // release resources of provided subclass of ref-counted class
                static_cast<release_resources_t>(&DerivedT::release_resources)
            );
    }

    ~ref_counted() noexcept {
        if (in_stack_unwind())
            discard_reference();
        // discard_reference() should have been called from derived classes by now
        assert(ref_data == nullptr || ref_data->ref_count > 1);
    }

protected:
    // static polymorphic interface:
    // "virtual" void release_resources() = 0;

    template <class extern_ref_counted_data>
    explicit ref_counted(extern_ref_counted_data* ref_data)
        : ref_data(ref_data) {
    }
    // optionally available to be enabled by subclasses
    ref_counted(std::nullptr_t)
        : ref_data(nullptr) {
    }
};

template <class D>
struct ref_counted<D>::ref_counted_data : public D::shared_data {
    int ref_count = 1;
};
