/// Source: https://github.com/pavel-kirienko/cavl
///
/// Cavl is a single-header C library providing an implementation of AVL tree suitable for deeply embedded systems.
/// To integrate it into your project, simply copy this file into your source tree.
/// You can define build option macros before including the header to customize the behavior.
/// All definitions are prefixed with cavl2 to avoid collisions with other major versions of the library.
/// Read the API docs below.
///
/// See also O1Heap <https://github.com/pavel-kirienko/o1heap> -- a deterministic memory manager for hard-real-time
/// high-integrity embedded systems.
///
/// Version history:
///
/// - v1.0: initial release.
/// - v2.0:
///   - Simplify the API and improve naming.
///   - The header file now bears the major version number, which simplifies vendoring: a project now can safely
///     depend on cavl without the risk of version compatibility issues.
///   - For the same reason as above, all definitions are now prefixed with cavl2 instead of cavl.
///   - Add optional CAVL2_T macro to allow overriding the cavl2_t type. This is needed for libudpard/libcanard/etc
///     and is generally useful because it allows library vendors to avoid exposing cavl via the library API.
///     Also add CAVL2_RELATION to simplify comparator implementations.
///   - Add the trivial factory definition because it is needed in nearly every application using cavl.
///   - New traversal function cavl2_next_greater() offering the same time complexity but without recursion/callbacks.
///
/// -------------------------------------------------------------------------------------------------------------------
///
/// Copyright (c) Pavel Kirienko <pavel@opencyphal.org>
///
/// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
/// documentation files (the "Software"), to deal in the Software without restriction, including without limitation
/// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
/// and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included in all copies or substantial portions of
/// the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
/// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
/// OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
/// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// ReSharper disable CppCStyleCast CppZeroConstantCanBeReplacedWithNullptr CppTooWideScopeInitStatement
// ReSharper disable CppRedundantElaboratedTypeSpecifier CppRedundantInlineSpecifier
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// If Cavl is used in throughput-critical code, then it is recommended to disable assertion checks as they may
/// be costly in terms of execution time.
#ifndef CAVL2_ASSERT
#if defined(CAVL2_NO_ASSERT) && CAVL2_NO_ASSERT
#define CAVL2_ASSERT(x) (void)0
#else
#include <assert.h>
#define CAVL2_ASSERT(x) assert(x)
#endif
#endif

#ifdef __cplusplus
// This is, strictly speaking, useless because we do not define any functions with external linkage here,
// but it tells static analyzers that what follows should be interpreted as C code rather than C++.
extern "C"
{
#endif

// ----------------------------------------         PUBLIC API SECTION         ----------------------------------------

/// CAVL2_T can be defined before including this header to provide a custom struct type for the node element.
/// The custom type must have the same fields as the default struct cavl2_t.
/// This option is useful if Cavl is integrated into a library without exposing it through the library API.
#ifndef CAVL2_T
/// The tree node/root. The user data is to be added through composition/inheritance.
/// The memory layout of this type is compatible with void*[4], which is useful if this type cannot be exposed in API.
/// Per standard convention, nodes that compare smaller are put on the left.
/// Usage example:
///     struct my_user_type_t {
///         struct cavl2_t base;  ///< Tree node. Should be the first element, otherwise, offsetof() will be needed.
///         ... user data ...
///     };
struct cavl2_t
{
    struct cavl2_t* up;    ///< Parent node, NULL in the root.
    struct cavl2_t* lr[2]; ///< Left child (lesser), right child (greater).
    int_fast8_t     bf;    ///< Balance factor is positive when right-heavy. Allowed values are {-1, 0, +1}.
};
#define CAVL2_T struct cavl2_t
#endif

#if defined(static_assert) || defined(__cplusplus)
static_assert(sizeof(CAVL2_T) <= sizeof(void* [4]), "Bad size");
#endif

/// The comparator result can be overridden to simplify comparator functions.
/// The type shall be a signed integer type.
/// Only three possible states of the result are considered: negative, zero, and positive; the magnitude is ignored.
#ifndef CAVL2_RELATION
#define CAVL2_RELATION ptrdiff_t
#endif
/// Returns POSITIVE if the search target is GREATER than the provided node, negative if smaller, zero on match (found).
typedef CAVL2_RELATION (*cavl2_comparator_t)(const void* user, const CAVL2_T* node);

/// If provided, the factory will be invoked when the sought node does not exist in the tree.
/// It is expected to return a new node that will be inserted immediately (without the need to traverse the tree again).
/// If the factory returns NULL or is not provided, the tree is not modified.
typedef CAVL2_T* (*cavl2_factory_t)(void* user);

/// Look for a node in the tree using the specified comparator. The worst-case complexity is O(log n).
/// - If the node is found (i.e., zero comparison result), return it.
/// - If the node is not found and the factory is NULL, return NULL.
/// - Otherwise, construct a new node using the factory; if the result is not NULL, insert it; return the result.
/// The user_comparator is passed into the comparator unmodified.
/// The user_factory is passed into the factory unmodified.
/// The root node may be replaced in the process iff the factory is not NULL and it returns a new node;
/// otherwise, the root node will not be modified.
/// If comparator is NULL, returns NULL.
static inline CAVL2_T* cavl2_find_or_insert(CAVL2_T** const          root,
                                            const void* const        user_comparator,
                                            const cavl2_comparator_t comparator,
                                            void* const              user_factory,
                                            const cavl2_factory_t    factory);

/// A convenience wrapper over cavl2_find_or_insert() that passes NULL factory, so the tree is never modified.
/// Since the tree is not modified, the root pointer is passed by value, unlike in the mutating version.
static inline CAVL2_T* cavl2_find(CAVL2_T* root, const void* const user_comparator, const cavl2_comparator_t comparator)
{
    return cavl2_find_or_insert(&root, user_comparator, comparator, NULL, NULL);
}

/// Remove the specified node from its tree. The root node may be replaced in the process.
/// The worst-case complexity is O(log n).
/// The function has no effect if either of the pointers are NULL.
/// If the node is not in the tree, the behavior is undefined; it may create cycles in the tree which is deadly.
/// It is safe to pass the result of cavl2_find/cavl2_find_or_insert directly as the second argument:
///     cavl2_remove(&root, cavl2_find(&root, user, search_comparator));
/// It is recommended to invalidate the pointers stored in the node after its removal.
static inline void cavl2_remove(CAVL2_T** const root, const CAVL2_T* const node);

/// Return the min-/max-valued node stored in the tree, depending on the flag. This is an extremely fast query.
/// Returns NULL iff the argument is NULL (i.e., the tree is empty). The worst-case complexity is O(log n).
static inline CAVL2_T* cavl2_extremum(CAVL2_T* const root, const bool maximum)
{
    CAVL2_T* result = NULL;
    CAVL2_T* c      = root;
    while (c != NULL) {
        result = c;
        c      = c->lr[maximum];
    }
    return result;
}

// clang-format off
/// Convenience wrappers for cavl2_extremum().
static inline CAVL2_T* cavl2_min(CAVL2_T* const root) { return cavl2_extremum(root, false); }
static inline CAVL2_T* cavl2_max(CAVL2_T* const root) { return cavl2_extremum(root, true);  }
// clang-format on

/// Returns the next greater node in the in-order traversal of the tree.
/// Does nothing and returns NULL if the argument is NULL. Behavior undefined if the node is not in the tree.
/// To use it, first invoke cavl2_min() to get the first node, then call this function repeatedly until it returns NULL:
///     for (CAVL2_T* p = cavl2_min(root); p != NULL; p = cavl2_next_greater(p)) {
///         ...
///     }
/// The asymptotic complexity for traversing the entire tree is O(n), identical to the traditional recursive traversal.
static inline CAVL2_T* cavl2_next_greater(CAVL2_T* const node)
{
    CAVL2_T* c = NULL;
    if (node != NULL) {
        if (node->lr[1] != NULL) {
            c = cavl2_min(node->lr[1]);
        } else {
            const CAVL2_T* n = node;
            CAVL2_T*       p = node->up;
            while ((p != NULL) && (p->lr[1] == n)) {
                n = p;
                p = p->up;
            }
            c = p;
        }
    }
    return c;
}

/// The trivial factory is useful in most applications. It simply returns the user pointed converted to CAVL2_T.
/// It is meant for use with cavl2_find_or_insert().
static inline CAVL2_T* cavl2_trivial_factory(void* const user)
{
    return (CAVL2_T*)user;
}

/// A convenience macro for use when a struct is a member of multiple AVL trees. For example:
///
///     struct my_type_t {
///         struct cavl2_t tree_a;
///         struct cavl2_t tree_b;
///         ...
///     };
///
/// If we only have tree_a, we don't need this helper because the C standard guarantees that the address of a struct
/// equals the address of its first member, always, so simply casting a tree node to (struct my_type_t*) yields
/// a valid pointer to the struct. However, if we have more than one tree nodes in a struct, for the other ones
/// we will need to subtract the offset of the tree node field from the address of the tree node to get to the owner.
/// This macro does exactly that. Example:
///
///     struct cavl2_t* tree_node_b = cavl2_find(...);  // whatever
///     if (tree_node_b == NULL) { ... }                // do something else
///     struct my_type_t* my_struct = CAVL2_TO_OWNER(tree_node_b, struct my_type_t, tree_b);
///
/// The result is undefined if the tree_node_ptr is not a valid pointer to the tree node. Check for NULL first.
#define CAVL2_TO_OWNER(tree_node_ptr, owner_type, owner_tree_node_field)                                     \
    ((owner_type*)(void*)(((char*)(tree_node_ptr)) - offsetof(owner_type, owner_tree_node_field))) // NOLINT

// ----------------------------------------     END OF PUBLIC API SECTION      ----------------------------------------
// ----------------------------------------      POLICE LINE DO NOT CROSS      ----------------------------------------

/// INTERNAL USE ONLY. Makes the '!r' child of node 'x' its parent; i.e., rotates 'x' toward 'r'.
static inline void _cavl2_rotate(CAVL2_T* const x, const bool r)
{
    CAVL2_ASSERT((x != NULL) && (x->lr[!r] != NULL) && ((x->bf >= -1) && (x->bf <= +1)));
    CAVL2_T* const z = x->lr[!r];
    if (x->up != NULL) {
        x->up->lr[x->up->lr[1] == x] = z;
    }
    z->up     = x->up;
    x->up     = z;
    x->lr[!r] = z->lr[r];
    if (x->lr[!r] != NULL) {
        x->lr[!r]->up = x;
    }
    z->lr[r] = x;
}

/// INTERNAL USE ONLY.
/// Accepts a node and how its balance factor needs to be changed -- either +1 or -1.
/// Returns the new node to replace the old one if tree rotation took place, same node otherwise.
static inline CAVL2_T* _cavl2_adjust_balance(CAVL2_T* const x, const bool increment)
{
    CAVL2_ASSERT((x != NULL) && ((x->bf >= -1) && (x->bf <= +1)));
    CAVL2_T*          out    = x;
    const int_fast8_t new_bf = (int_fast8_t)(x->bf + (increment ? +1 : -1));
    if ((new_bf < -1) || (new_bf > 1)) {
        const bool        r    = new_bf < 0;  // bf<0 if left-heavy --> right rotation is needed.
        const int_fast8_t sign = r ? +1 : -1; // Positive if we are rotating right.
        CAVL2_T* const    z    = x->lr[!r];
        CAVL2_ASSERT(z != NULL);   // Heavy side cannot be empty.  NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
        if ((z->bf * sign) <= 0) { // Parent and child are heavy on the same side or the child is balanced.
            out = z;
            _cavl2_rotate(x, r);
            if (0 == z->bf) {
                x->bf = (int_fast8_t)(-sign);
                z->bf = (int_fast8_t)(+sign);
            } else {
                x->bf = 0;
                z->bf = 0;
            }
        } else { // Otherwise, the child needs to be rotated in the opposite direction first.
            CAVL2_T* const y = z->lr[r];
            CAVL2_ASSERT(y != NULL); // Heavy side cannot be empty.
            out = y;
            _cavl2_rotate(z, !r);
            _cavl2_rotate(x, r);
            if ((y->bf * sign) < 0) {
                x->bf = (int_fast8_t)(+sign);
                y->bf = 0;
                z->bf = 0;
            } else if ((y->bf * sign) > 0) {
                x->bf = 0;
                y->bf = 0;
                z->bf = (int_fast8_t)(-sign);
            } else {
                x->bf = 0;
                z->bf = 0;
            }
        }
    } else {
        x->bf = new_bf; // Balancing not needed, just update the balance factor and call it a day.
    }
    return out;
}

/// INTERNAL USE ONLY.
/// Takes the culprit node (the one that is added); returns NULL or the root of the tree (possibly new one).
/// When adding a new node, set its balance factor to zero and call this function to propagate the changes upward.
static inline CAVL2_T* _cavl2_retrace_on_growth(CAVL2_T* const added)
{
    CAVL2_ASSERT((added != NULL) && (0 == added->bf));
    CAVL2_T* c = added;     // Child
    CAVL2_T* p = added->up; // Parent
    while (p != NULL) {
        const bool r = p->lr[1] == c; // c is the right child of parent
        CAVL2_ASSERT(p->lr[r] == c);
        c = _cavl2_adjust_balance(p, r);
        p = c->up;
        if (0 ==
            c->bf) { // The height change of the subtree made this parent perfectly balanced (as all things should be),
            break;   // hence, the height of the outer subtree is unchanged, so upper balance factors are unchanged.
        }
    }
    CAVL2_ASSERT(c != NULL);
    return (NULL == p) ? c : NULL; // New root or nothing.
}

static inline CAVL2_T* cavl2_find_or_insert(CAVL2_T** const          root,
                                            const void* const        user_comparator,
                                            const cavl2_comparator_t comparator,
                                            void* const              user_factory,
                                            const cavl2_factory_t    factory)
{
    CAVL2_T* out = NULL;
    if ((root != NULL) && (comparator != NULL)) {
        CAVL2_T*  up = *root;
        CAVL2_T** n  = root;
        while (*n != NULL) {
            const CAVL2_RELATION cmp = comparator(user_comparator, *n);
            if (0 == cmp) {
                out = *n;
                break;
            }
            up = *n;
            n  = &(*n)->lr[cmp > 0];
            CAVL2_ASSERT((NULL == *n) || ((*n)->up == up));
        }
        if (NULL == out) {
            out = (NULL == factory) ? NULL : factory(user_factory);
            if (out != NULL) {
                *n                = out; // Overwrite the pointer to the new node in the parent node.
                out->lr[0]        = NULL;
                out->lr[1]        = NULL;
                out->up           = up;
                out->bf           = 0;
                CAVL2_T* const rt = _cavl2_retrace_on_growth(out);
                if (rt != NULL) {
                    *root = rt;
                }
            }
        }
    }
    return out;
}

static inline void cavl2_remove(CAVL2_T** const root, const CAVL2_T* const node)
{
    if ((root != NULL) && (node != NULL)) {
        CAVL2_ASSERT(*root != NULL); // Otherwise, the node would have to be NULL.
        CAVL2_ASSERT((node->up != NULL) || (node == *root));
        CAVL2_T* p = NULL;  // The lowest parent node that suffered a shortening of its subtree.
        bool     r = false; // Which side of the above was shortened.
        // The first step is to update the topology and remember the node where to start the retracing from later.
        // Balancing is not performed yet so we may end up with an unbalanced tree.
        if ((node->lr[0] != NULL) && (node->lr[1] != NULL)) {
            CAVL2_T* const re = cavl2_extremum(node->lr[1], false);
            CAVL2_ASSERT((re != NULL) && (NULL == re->lr[0]) && (re->up != NULL));
            re->bf        = node->bf;
            re->lr[0]     = node->lr[0];
            re->lr[0]->up = re;
            if (re->up != node) {
                p = re->up; // Retracing starts with the ex-parent of our replacement node.
                CAVL2_ASSERT(p->lr[0] == re);
                p->lr[0] = re->lr[1]; // Reducing the height of the left subtree here.
                if (p->lr[0] != NULL) {
                    p->lr[0]->up = p;
                }
                re->lr[1]     = node->lr[1];
                re->lr[1]->up = re;
                r             = false;
            } else // In this case, we are reducing the height of the right subtree, so r=1.
            {
                p = re;   // Retracing starts with the replacement node itself as we are deleting its parent.
                r = true; // The right child of the replacement node remains the same so we don't bother relinking it.
            }
            re->up = node->up;
            if (re->up != NULL) {
                re->up->lr[re->up->lr[1] == node] = re; // Replace link in the parent of node.
            } else {
                *root = re;
            }
        } else { // Either or both of the children are NULL.
            p             = node->up;
            const bool rr = node->lr[1] != NULL;
            if (node->lr[rr] != NULL) {
                node->lr[rr]->up = p;
            }
            if (p != NULL) {
                r        = p->lr[1] == node;
                p->lr[r] = node->lr[rr];
                if (p->lr[r] != NULL) {
                    p->lr[r]->up = p;
                }
            } else {
                *root = node->lr[rr];
            }
        }
        // Now that the topology is updated, perform the retracing to restore balance. We climb up adjusting the
        // balance factors until we reach the root or a parent whose balance factor becomes plus/minus one, which
        // means that that parent was able to absorb the balance delta; in other words, the height of the outer
        // subtree is unchanged, so upper balance factors shall be kept unchanged.
        if (p != NULL) {
            CAVL2_T* c = NULL;
            for (;;) {
                c = _cavl2_adjust_balance(p, !r);
                p = c->up;
                if ((c->bf != 0) || (NULL == p)) { // Reached the root or the height difference is absorbed by c.
                    break;
                }
                r = p->lr[1] == c;
            }
            if (NULL == p) {
                CAVL2_ASSERT(c != NULL);
                *root = c;
            }
        }
    }
}

#ifdef __cplusplus
}
#endif
