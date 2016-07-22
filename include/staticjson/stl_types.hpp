#pragma once
#include <staticjson/basic.hpp>

#include <deque>
#include <list>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

namespace staticjson
{
template <class ArrayType>
class ArrayHandler : public BaseHandler
{
public:
    typedef typename ArrayType::value_type ElementType;

protected:
    ElementType element;
    Handler<ElementType> internal;
    ArrayType* m_value;
    int depth = 0;

protected:
    void set_element_error() { the_error.reset(new error::ArrayElementError(m_value->size())); }

    bool precheck(const char* type)
    {
        if (depth <= 0)
        {
            the_error.reset(new error::TypeMismatchError(type_name(), type));
            return false;
        }
        return true;
    }

    bool postcheck(bool success)
    {
        if (!success)
        {
            set_element_error();
            return false;
        }
        if (internal.is_parsed())
        {
            m_value->emplace_back(std::move(element));
            element = ElementType();
            internal.prepare_for_reuse();
        }
        return true;
    }

    void reset() override
    {
        element = ElementType();
        internal.prepare_for_reuse();
        depth = 0;
    }

public:
    explicit ArrayHandler(ArrayType* value) : element(), internal(&element), m_value(value) {}

    bool Null() override { return precheck("null") && postcheck(internal.Null()); }

    bool Bool(bool b) override { return precheck("bool") && postcheck(internal.Bool(b)); }

    bool Int(int i) override { return precheck("int") && postcheck(internal.Int(i)); }

    bool Uint(unsigned i) override { return precheck("unsigned") && postcheck(internal.Uint(i)); }

    bool Int64(std::int64_t i) override
    {
        return precheck("int64_t") && postcheck(internal.Int64(i));
    }

    bool Uint64(std::uint64_t i) override
    {
        return precheck("uint64_t") && postcheck(internal.Uint64(i));
    }

    bool Double(double d) override { return precheck("double") && postcheck(internal.Double(d)); }

    bool String(const char* str, SizeType length, bool copy) override
    {
        return precheck("string") && postcheck(internal.String(str, length, copy));
    }

    bool Key(const char* str, SizeType length, bool copy) override
    {
        return precheck("object") && postcheck(internal.Key(str, length, copy));
    }

    bool StartObject() override { return precheck("object") && postcheck(internal.StartObject()); }

    bool EndObject(SizeType length) override
    {
        return precheck("object") && postcheck(internal.EndObject(length));
    }

    bool StartArray() override
    {
        ++depth;
        if (depth > 1)
            return postcheck(internal.StartArray());
        return true;
    }

    bool EndArray(SizeType length) override
    {
        --depth;

        // When depth > 1, this event should be forwarded to the element
        if (depth > 0)
            return postcheck(internal.EndArray(length));

        this->parsed = true;
        return true;
    }

    bool reap_error(ErrorStack& stk) override
    {
        if (!the_error)
            return false;
        stk.push(the_error.release());
        internal.reap_error(stk);
        return true;
    }

    bool write(IHandler* output) const override
    {
        if (!output->StartArray())
            return false;
        for (auto&& e : *m_value)
        {
            Handler<ElementType> h(&e);
            if (!h.write(output))
                return false;
        }
        return output->EndArray(m_value->size());
    }

    void generate_schema(Value& output, MemoryPoolAllocator& alloc) const override
    {
        output.SetObject();
        output.AddMember(rapidjson::StringRef("type"), rapidjson::StringRef("array"), alloc);
        Value items;
        internal.generate_schema(items, alloc);
        output.AddMember(rapidjson::StringRef("items"), items, alloc);
    }
};

template <class T>
class Handler<std::vector<T>> : public ArrayHandler<std::vector<T>>
{
public:
    explicit Handler(std::vector<T>* value) : ArrayHandler<std::vector<T>>(value) {}

    std::string type_name() const override
    {
        return "std::vector<" + this->internal.type_name() + ">";
    }
};

template <class T>
class Handler<std::deque<T>> : public ArrayHandler<std::deque<T>>
{
public:
    explicit Handler(std::deque<T>* value) : ArrayHandler<std::deque<T>>(value) {}

    std::string type_name() const override
    {
        return "std::deque<" + this->internal.type_name() + ">";
    }
};

template <class T>
class Handler<std::list<T>> : public ArrayHandler<std::list<T>>
{
public:
    explicit Handler(std::list<T>* value) : ArrayHandler<std::list<T>>(value) {}

    std::string type_name() const override
    {
        return "std::list<" + this->internal.type_name() + ">";
    }
};

template <class PointerType>
class PointerHandler : public BaseHandler
{
public:
    typedef typename std::pointer_traits<PointerType>::element_type ElementType;

protected:
    mutable PointerType* m_value;
    mutable std::unique_ptr<Handler<ElementType>> internal_handler;
    int depth = 0;

protected:
    explicit PointerHandler(PointerType* value) : m_value(value) {}

    void initialize()
    {
        if (!internal_handler)
        {
            m_value->reset(new ElementType());
            internal_handler.reset(new Handler<ElementType>(m_value->get()));
        }
    }

    void reset() override
    {
        depth = 0;
        internal_handler.reset();
        m_value->reset();
    }

    bool postcheck(bool success)
    {
        if (success)
            this->parsed = internal_handler->is_parsed();
        return success;
    }

public:
    bool Null() override
    {
        if (depth == 0)
        {
            m_value->reset();
            this->parsed = true;
            return true;
        }
        else
        {
            initialize();
            return postcheck(internal_handler->Null());
        }
    }

    bool write(IHandler* out) const override
    {
        if (!m_value || !m_value->get())
        {
            return out->Null();
        }
        if (!internal_handler)
        {
            internal_handler.reset(new Handler<ElementType>(m_value->get()));
        }
        return internal_handler->write(out);
    }

    void generate_schema(Value& output, MemoryPoolAllocator& alloc) const override
    {
        const_cast<PointerHandler<PointerType>*>(this)->initialize();
        output.SetObject();
        Value anyOf(rapidjson::kArrayType);
        Value nullDescriptor(rapidjson::kObjectType);
        nullDescriptor.AddMember(rapidjson::StringRef("type"), rapidjson::StringRef("null"), alloc);
        Value descriptor;
        internal_handler->generate_schema(descriptor, alloc);
        anyOf.PushBack(nullDescriptor, alloc);
        anyOf.PushBack(descriptor, alloc);
        output.AddMember(rapidjson::StringRef("anyOf"), anyOf, alloc);
    }

    bool Bool(bool b) override
    {
        initialize();
        return postcheck(internal_handler->Bool(b));
    }

    bool Int(int i) override
    {
        initialize();
        return postcheck(internal_handler->Int(i));
    }

    bool Uint(unsigned i) override
    {
        initialize();
        return postcheck(internal_handler->Uint(i));
    }

    bool Int64(std::int64_t i) override
    {
        initialize();
        return postcheck(internal_handler->Int64(i));
    }

    bool Uint64(std::uint64_t i) override
    {
        initialize();
        return postcheck(internal_handler->Uint64(i));
    }

    bool Double(double i) override
    {
        initialize();
        return postcheck(internal_handler->Double(i));
    }

    bool String(const char* str, SizeType len, bool copy) override
    {
        initialize();
        return postcheck(internal_handler->String(str, len, copy));
    }

    bool Key(const char* str, SizeType len, bool copy) override
    {
        initialize();
        return postcheck(internal_handler->Key(str, len, copy));
    }

    bool StartObject() override
    {
        initialize();
        ++depth;
        return internal_handler->StartObject();
    }

    bool EndObject(SizeType len) override
    {
        initialize();
        --depth;
        return postcheck(internal_handler->EndObject(len));
    }

    bool StartArray() override
    {
        initialize();
        ++depth;
        return postcheck(internal_handler->StartArray());
    }

    bool EndArray(SizeType len) override
    {
        initialize();
        --depth;
        return postcheck(internal_handler->EndArray(len));
    }

    bool has_error() const override { return internal_handler && internal_handler->has_error(); }

    bool reap_error(ErrorStack& stk) override
    {
        return internal_handler && internal_handler->reap_error(stk);
    }
};

template <class T, class Deleter>
class Handler<std::unique_ptr<T, Deleter>> : public PointerHandler<std::unique_ptr<T, Deleter>>
{
public:
    explicit Handler(std::unique_ptr<T, Deleter>* value)
        : PointerHandler<std::unique_ptr<T, Deleter>>(value)
    {
    }

    std::string type_name() const override
    {
        if (this->internal_handler)
        {
            return "std::unique_ptr<" + this->internal_handler->type_name() + ">";
        }
        return "std::unique_ptr";
    }
};

template <class T>
class Handler<std::shared_ptr<T>> : public PointerHandler<std::shared_ptr<T>>
{
public:
    explicit Handler(std::shared_ptr<T>* value) : PointerHandler<std::shared_ptr<T>>(value) {}

    std::string type_name() const override
    {
        if (this->internal_handler)
        {
            return "std::shared_ptr<" + this->internal_handler->type_name() + ">";
        }
        return "std::shared_ptr";
    }
};

template <class MapType>
class MapHandler : public BaseHandler
{
protected:
    typedef typename MapType::mapped_type ElementType;

protected:
    ElementType element;
    Handler<ElementType> internal_handler;
    MapType* m_value;
    std::string current_key;
    int depth = 0;

protected:
    void reset() override
    {
        element = ElementType();
        current_key.clear();
        internal_handler.prepare_for_reuse();
        depth = 0;
    }

    bool precheck(const char* type)
    {
        if (depth <= 0)
        {
            set_type_mismatch(type);
            return false;
        }
        return true;
    }

    bool postcheck(bool success)
    {
        if (!success)
        {
            the_error.reset(new error::ObjectMemberError(current_key));
        }
        else
        {
            if (internal_handler.is_parsed())
            {
                m_value->emplace(std::move(current_key), std::move(element));
                element = ElementType();
                internal_handler.prepare_for_reuse();
            }
        }
        return success;
    }

public:
    explicit MapHandler(MapType* value) : element(), internal_handler(&element), m_value(value) {}

    bool Null() override { return precheck("null") && postcheck(internal_handler.Null()); }

    bool Bool(bool b) override { return precheck("bool") && postcheck(internal_handler.Bool(b)); }

    bool Int(int i) override { return precheck("int") && postcheck(internal_handler.Int(i)); }

    bool Uint(unsigned i) override
    {
        return precheck("unsigned") && postcheck(internal_handler.Uint(i));
    }

    bool Int64(std::int64_t i) override
    {
        return precheck("int64_t") && postcheck(internal_handler.Int64(i));
    }

    bool Uint64(std::uint64_t i) override
    {
        return precheck("uint64_t") && postcheck(internal_handler.Uint64(i));
    }

    bool Double(double d) override
    {
        return precheck("double") && postcheck(internal_handler.Double(d));
    }

    bool String(const char* str, SizeType length, bool copy) override
    {
        return precheck("string") && postcheck(internal_handler.String(str, length, copy));
    }

    bool Key(const char* str, SizeType length, bool copy) override
    {
        if (depth > 1)
            return postcheck(internal_handler.Key(str, length, copy));

        current_key.assign(str, length);
        return true;
    }

    bool StartArray() override
    {
        return precheck("array") && postcheck(internal_handler.StartArray());
    }

    bool EndArray(SizeType length) override
    {
        return precheck("array") && postcheck(internal_handler.EndArray(length));
    }

    bool StartObject() override
    {
        ++depth;
        if (depth > 1)
            return postcheck(internal_handler.StartObject());
        return true;
    }

    bool EndObject(SizeType length) override
    {
        --depth;
        if (depth > 0)
            return postcheck(internal_handler.EndObject(length));
        this->parsed = true;
        return true;
    }

    bool reap_error(ErrorStack& errs) override
    {
        if (!this->the_error)
            return false;

        errs.push(this->the_error.release());
        internal_handler.reap_error(errs);
        return true;
    }

    bool write(IHandler* out) const override
    {
        if (!out->StartObject())
            return false;
        for (auto&& pair : *m_value)
        {
            if (!out->Key(pair.first.data(), pair.first.size(), true))
                return false;
            Handler<ElementType> h(&pair.second);
            if (!h.write(out))
                return false;
        }
        return out->EndObject(m_value->size());
    }

    void generate_schema(Value& output, MemoryPoolAllocator& alloc) const override
    {
        Value internal_schema;
        internal_handler.generate_schema(internal_schema, alloc);
        output.SetObject();
        output.AddMember(rapidjson::StringRef("type"), rapidjson::StringRef("object"), alloc);

        Value empty_obj(rapidjson::kObjectType);
        output.AddMember(rapidjson::StringRef("properties"), empty_obj, alloc);
        output.AddMember(rapidjson::StringRef("additionalProperties"), internal_schema, alloc);
    }
};

template <class T, class Hash, class Equal>
class Handler<std::unordered_map<std::string, T, Hash, Equal>>
    : public MapHandler<std::unordered_map<std::string, T, Hash, Equal>>
{
public:
    explicit Handler(std::unordered_map<std::string, T, Hash, Equal>* value)
        : MapHandler<std::unordered_map<std::string, T, Hash, Equal>>(value)
    {
    }

    std::string type_name() const override
    {
        return "std::unordered_map<std::string, " + this->internal_handler.type_name() + ">";
    }
};

template <class T, class Hash, class Equal>
class Handler<std::map<std::string, T, Hash, Equal>>
    : public MapHandler<std::map<std::string, T, Hash, Equal>>
{
public:
    explicit Handler(std::map<std::string, T, Hash, Equal>* value)
        : MapHandler<std::map<std::string, T, Hash, Equal>>(value)
    {
    }

    std::string type_name() const override
    {
        return "std::map<std::string, " + this->internal_handler.type_name() + ">";
    }
};

template <class T, class Hash, class Equal>
class Handler<std::unordered_multimap<std::string, T, Hash, Equal>>
    : public MapHandler<std::unordered_multimap<std::string, T, Hash, Equal>>
{
public:
    explicit Handler(std::unordered_multimap<std::string, T, Hash, Equal>* value)
        : MapHandler<std::unordered_multimap<std::string, T, Hash, Equal>>(value)
    {
    }

    std::string type_name() const override
    {
        return "std::unordered_mulitimap<std::string, " + this->internal_handler.type_name() + ">";
    }
};

template <class T, class Hash, class Equal>
class Handler<std::multimap<std::string, T, Hash, Equal>>
    : public MapHandler<std::multimap<std::string, T, Hash, Equal>>
{
public:
    explicit Handler(std::multimap<std::string, T, Hash, Equal>* value)
        : MapHandler<std::multimap<std::string, T, Hash, Equal>>(value)
    {
    }

    std::string type_name() const override
    {
        return "std::multimap<std::string, " + this->internal_handler.type_name() + ">";
    }
};
}