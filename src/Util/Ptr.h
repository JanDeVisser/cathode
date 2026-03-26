/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <Util/Logging.h>

template<class T, class Repo = std::vector<T>>
class Ptr {
public:
    Ptr() = default;
    Ptr(Ptr const &) = default;

    Ptr(Repo *repo, size_t id)
        : repo(repo)
        , id(id)
    {
        assert(repo != nullptr);
        assert(id < repo->size());
    }

    Ptr(Repo *repo)
        : repo(repo)
    {
        if (repo != nullptr) {
            assert(!repo->empty());
            id = repo->size() - 1;
        }
    }

    template<typename... Args>
    Ptr(Repo *repo, Args &&...args)
        : repo(repo)
    {
        assert(repo != nullptr);
        repo->emplace_back(std::forward<Args>(args)...);
        id = repo->size() - 1;
        repo->back().id = *this;
    }

    Ptr(std::nullptr_t const &)
        : repo(nullptr)
        , id()
    {
    }

    Ptr &operator=(Ptr const &other) = default;
    Ptr &operator=(std::nullptr_t const &)
    {
        repo = nullptr;
        id.reset();
        return *this;
    }

    //    T const *operator->() const
    //    {
    //        assert(repo != nullptr);
    //        assert(id.has_value() && id.value() < repo->size());
    //        return &(*repo)[id.value()];
    //    }

    T *operator->() const
    {
        assert(repo != nullptr);
        assert(id.has_value() && id.value() < repo->size());
        return const_cast<T *>(&((*repo)[id.value()]));
    }

    T const &operator*() const
    {
        assert(repo != nullptr);
        assert(id.has_value() && id.value() < repo->size());
        return (*repo)[id.value()];
    }

    T &operator*()
    {
        assert(repo != nullptr);
        assert(id.has_value() && id.value() < repo->size());
        return (*repo)[id.value()];
    }

    operator bool() const
    {
        return repo != nullptr && id.has_value();
    }

    bool operator==(Ptr const &other) const
    {
        if (repo != other.repo) {
            return false;
        }
        if (id.has_value() ^ other.id.has_value()) {
            return false;
        }
        if (!id.has_value()) {
            return true;
        }
        return (id.value() == other.id.value());
    }

    bool operator==(std::nullptr_t const &) const
    {
        return (repo == nullptr) || !id.has_value();
    }

    bool operator==(T const *other) const
    {
        return repo != nullptr && id.has_value() && &(*repo)[id.value()] == other;
    }

    bool operator!=(T const *other) const
    {
        return repo == nullptr || !id.has_value() || &(*repo)[id.value()] != other;
    }

    bool operator!=(auto const &other) const
    {
        return !(*this == other);
    }

    bool operator<(Ptr const &other) const
    {
        if (repo != other.repo) {
            return repo < other.repo;
        }
        if (!id.has_value()) {
            return false;
        }
        if (!other.id.has_value()) {
            return true;
        }
        return id.value() < other.id.value();
    }

    Repo                 *repo;
    std::optional<size_t> id;
};
