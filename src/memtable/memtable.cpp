#include "memtable/memtable.h"
#include <random>
#include <iostream>
#include <cassert>

namespace kv_engine {

// MemTable::Impl implementation
MemTable::Impl::Impl(const Options& options)
    : max_level(options.max_level)
    , probability(options.probability)
    , comparator(options.comparator)
    , rng(std::random_device{}())
    , dist(0.0, 1.0) {
    // Create head node with max_level forward pointers
    head = new Node("", "", max_level + 1, 0);
}

MemTable::Impl::~Impl() {
    // Delete all nodes
    Node* current = head->forward[0];
    while (current) {
        Node* next = current->forward[0];
        delete current;
        current = next;
    }
    delete head;
}

int MemTable::Impl::RandomLevel() {
    int level = 1;
    while (level < max_level && dist(rng) < probability) {
        level++;
    }
    return level;
}

int MemTable::Impl::CompareKeys(const Slice& a, const Slice& b) const {
    if (comparator) {
        return comparator->Compare(a, b);
    }
    return a.compare(b);
}

MemTable::Node* MemTable::Impl::FindGreaterOrEqual(const Slice& key, std::vector<Node*>* prev) const {
    Node* current = head;
    int level = current_max_level - 1;
    
    if (prev) {
        prev->assign(max_level + 1, nullptr);
    }
    
    while (true) {
        Node* next = current->forward[level];
        if (next && CompareKeys(Slice(next->key), key) < 0) {
            current = next;
        } else {
            if (prev) {
                (*prev)[level] = current;
            }
            if (level == 0) {
                return next;
            }
            level--;
        }
    }
}

MemTable::Node* MemTable::Impl::FindLessThan(const Slice& key) const {
    Node* current = head;
    int level = current_max_level - 1;
    
    while (true) {
        Node* next = current->forward[level];
        if (next && CompareKeys(Slice(next->key), key) < 0) {
            current = next;
        } else {
            if (level == 0) {
                return current;
            }
            level--;
        }
    }
}

MemTable::Node* MemTable::Impl::FindLast() const {
    Node* current = head;
    int level = current_max_level - 1;
    
    while (true) {
        Node* next = current->forward[level];
        if (next) {
            current = next;
        } else {
            if (level == 0) {
                return current;
            }
            level--;
        }
    }
}

void MemTable::Impl::InsertNode(Node* node, const std::vector<Node*>& prev) {
    int level = node->forward.size() - 1;
    for (int i = 0; i <= level; ++i) {
        node->forward[i] = prev[i]->forward[i];
        prev[i]->forward[i] = node;
    }
    if (level + 1 > current_max_level) {
        current_max_level = level + 1;
    }
}

bool MemTable::Impl::DeleteNode(const Slice& key, std::vector<Node*>* prev) {
    Node* current = head;
    int level = current_max_level - 1;
    bool found = false;
    
    if (prev) {
        prev->assign(max_level + 1, nullptr);
    }
    
    while (true) {
        Node* next = current->forward[level];
        if (next && CompareKeys(Slice(next->key), key) < 0) {
            current = next;
        } else {
            if (prev) {
                (*prev)[level] = current;
            }
            if (level == 0) {
                if (next && CompareKeys(Slice(next->key), key) == 0) {
                    found = true;
                    // Unlink the node
                    for (int i = 0; i < current_max_level; ++i) {
                        if (prev && (*prev)[i]->forward[i] == next) {
                            (*prev)[i]->forward[i] = next->forward[i];
                        }
                    }
                    delete next;
                    // Update current_max_level
                    while (current_max_level > 1 && head->forward[current_max_level - 1] == nullptr) {
                        current_max_level--;
                    }
                }
                break;
            }
            level--;
        }
    }
    return found;
}

MemTable::MemTable(const Options& options)
    : impl_(std::make_unique<Impl>(options)), options_(options) {}

MemTable::~MemTable() = default;

MemTable::MemTable(MemTable&& other) noexcept
    : impl_(std::move(other.impl_)), options_(other.options_),
      size_(other.size_.load()), memory_usage_(other.memory_usage_.load()),
      max_sequence_(other.max_sequence_.load()) {
    other.size_.store(0);
    other.memory_usage_.store(0);
    other.max_sequence_.store(0);
}

MemTable& MemTable::operator=(MemTable&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
        options_ = other.options_;
        size_.store(other.size_.load());
        memory_usage_.store(other.memory_usage_.load());
        max_sequence_.store(other.max_sequence_.load());
        
        other.size_.store(0);
        other.memory_usage_.store(0);
        other.max_sequence_.store(0);
    }
    return *this;
}

Status MemTable::Put(const Slice& key, const Slice& value, uint64_t sequence_number) {
    std::vector<typename Impl::Node*> prev;
    typename Impl::Node* existing = impl_->FindGreaterOrEqual(key, &prev);
    
    if (existing && impl_->CompareKeys(Slice(existing->key), key) == 0) {
        // Update existing
        size_t old_value_size = existing->value.size();
        existing->value = value.ToString();
        existing->sequence_number = sequence_number;
        existing->is_deleted = false;
        
        memory_usage_.fetch_add(value.size() - old_value_size);
        max_sequence_.store(std::max(max_sequence_.load(), sequence_number));
        return Status::OK();
    }
    
    // Insert new node
    int level = impl_->RandomLevel();
    typename Impl::Node* new_node = new typename Impl::Node(
        key.ToString(), value.ToString(), level, sequence_number, false);
    
    impl_->InsertNode(new_node, prev);
    
    size_.fetch_add(1);
    memory_usage_.fetch_add(key.size() + value.size() + sizeof(typename Impl::Node) + level * sizeof(void*));
    max_sequence_.store(std::max(max_sequence_.load(), sequence_number));
    
    return Status::OK();
}

Status MemTable::Get(const Slice& key, std::string* value, uint64_t* sequence_number) const {
    typename Impl::Node* node = impl_->FindGreaterOrEqual(key, nullptr);
    
    if (node && impl_->CompareKeys(Slice(node->key), key) == 0) {
        if (node->is_deleted) {
            return Status::NotFound("Key deleted");
        }
        *value = node->value;
        if (sequence_number) {
            *sequence_number = node->sequence_number;
        }
        return Status::OK();
    }
    return Status::NotFound("Key not found");
}

Status MemTable::Delete(const Slice& key, uint64_t sequence_number) {
    // Insert a tombstone
    std::vector<typename Impl::Node*> prev;
    typename Impl::Node* existing = impl_->FindGreaterOrEqual(key, &prev);
    
    if (existing && impl_->CompareKeys(Slice(existing->key), key) == 0) {
        // Update existing to tombstone
        if (!existing->is_deleted) {
            size_.fetch_sub(1);
            memory_usage_.fetch_sub(existing->value.size());
            existing->value.clear();
        }
        existing->is_deleted = true;
        existing->sequence_number = sequence_number;
        max_sequence_.store(std::max(max_sequence_.load(), sequence_number));
        return Status::OK();
    }
    
    // Insert new tombstone
    int level = impl_->RandomLevel();
    typename Impl::Node* new_node = new typename Impl::Node(
        key.ToString(), "", level, sequence_number, true);
    
    impl_->InsertNode(new_node, prev);
    
    memory_usage_.fetch_add(key.size() + sizeof(typename Impl::Node) + level * sizeof(void*));
    max_sequence_.store(std::max(max_sequence_.load(), sequence_number));
    
    return Status::OK();
}

bool MemTable::Contains(const Slice& key) const {
    typename Impl::Node* node = impl_->FindGreaterOrEqual(key, nullptr);
    return node && impl_->CompareKeys(Slice(node->key), key) == 0;
}

// Iterator implementation
struct MemTableIterator::Impl {
    MemTable* table = nullptr;
    typename MemTable::Impl::Node* current = nullptr;
    Status status_;
    
    explicit Impl(MemTable* t) : table(t) {}
    
    void FindFirst() {
        current = table->impl_->head->forward[0];
    }
    
    void FindLast() {
        current = table->impl_->FindLast();
        if (current == table->impl_->head) {
            current = nullptr;
        }
    }
    
    void FindGE(const Slice& target) {
        current = table->impl_->FindGreaterOrEqual(target, nullptr);
    }
    
    bool AtEnd() const {
        return current == nullptr;
    }
};

MemTableIterator::MemTableIterator(MemTable* table)
    : impl_(std::make_unique<Impl>(table)) {
    impl_->FindFirst();
}

MemTableIterator::~MemTableIterator() = default;

MemTableIterator::MemTableIterator(MemTableIterator&& other) noexcept = default;
MemTableIterator& MemTableIterator::operator=(MemTableIterator&& other) noexcept = default;

bool MemTableIterator::Valid() const {
    return !impl_->AtEnd() && impl_->status_.ok();
}

void MemTableIterator::SeekToFirst() {
    impl_->FindFirst();
}

void MemTableIterator::SeekToLast() {
    impl_->FindLast();
}

void MemTableIterator::Seek(const Slice& target) {
    impl_->FindGE(target);
}

void MemTableIterator::Next() {
    if (impl_->AtEnd()) return;
    impl_->current = impl_->current->forward[0];
}

void MemTableIterator::Prev() {
    // Skip list doesn't efficiently support Prev without backward pointers
    impl_->status_ = Status::NotSupported("Prev not implemented for skip list iterator");
}

Slice MemTableIterator::key() const {
    if (impl_->AtEnd()) return Slice();
    return Slice(impl_->current->key);
}

Slice MemTableIterator::value() const {
    if (impl_->AtEnd()) return Slice();
    return Slice(impl_->current->value);
}

uint64_t MemTableIterator::sequence_number() const {
    if (impl_->AtEnd()) return 0;
    return impl_->current->sequence_number;
}

bool MemTableIterator::is_deleted() const {
    if (impl_->AtEnd()) return false;
    return impl_->current->is_deleted;
}

Status MemTableIterator::status() const {
    return impl_->status_;
}

MemTableIterator* MemTable::NewIterator() {
    return new MemTableIterator(this);
}

void MemTable::Print() const {
    std::cout << "MemTable: size=" << size_.load() 
              << ", memory=" << memory_usage_.load() 
              << ", max_seq=" << max_sequence_.load() << "\n";
    
    typename Impl::Node* current = impl_->head->forward[0];
    while (current) {
        std::cout << "  " << current->key << " -> " 
                  << (current->is_deleted ? "[DELETED]" : current->value)
                  << " (seq=" << current->sequence_number << ")\n";
        current = current->forward[0];
    }
}

bool MemTable::Verify() const {
    // Verify skip list ordering
    typename Impl::Node* current = impl_->head->forward[0];
    while (current && current->forward[0]) {
        if (impl_->CompareKeys(Slice(current->key), Slice(current->forward[0]->key)) >= 0) {
            return false;
        }
        current = current->forward[0];
    }
    return true;
}

} // namespace kv_engine