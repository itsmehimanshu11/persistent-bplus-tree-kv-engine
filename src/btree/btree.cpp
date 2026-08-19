#include "btree/btree.h"

#include <algorithm>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace kv_engine {

struct BPlusTree::Impl {
    std::vector<std::unique_ptr<BTreeNode>> nodes;
    std::vector<bool> node_is_leaf;
    std::vector<bool> node_free;
    std::vector<uint64_t> free_list;
    uint64_t next_node_id = 1;
    size_t max_keys_per_node = 128;

    explicit Impl(size_t max_keys) : max_keys_per_node(std::max<size_t>(3, max_keys)) {
        nodes.push_back(nullptr); // id 0 is invalid
        node_is_leaf.push_back(false);
        node_free.push_back(true);
    }

    uint64_t AllocateNode(bool is_leaf) {
        uint64_t id;
        if (!free_list.empty()) {
            id = free_list.back();
            free_list.pop_back();
            nodes[id] = is_leaf
                ? std::unique_ptr<BTreeNode>(new LeafNode(max_keys_per_node))
                : std::unique_ptr<BTreeNode>(new InternalNode(max_keys_per_node));
            node_is_leaf[id] = is_leaf;
            node_free[id] = false;
            return id;
        }
        id = next_node_id++;
        nodes.push_back(is_leaf
            ? std::unique_ptr<BTreeNode>(new LeafNode(max_keys_per_node))
            : std::unique_ptr<BTreeNode>(new InternalNode(max_keys_per_node)));
        node_is_leaf.push_back(is_leaf);
        node_free.push_back(false);
        return id;
    }

    void FreeNode(uint64_t id) {
        if (id != 0 && id < nodes.size() && !node_free[id]) {
            nodes[id].reset();
            node_free[id] = true;
            free_list.push_back(id);
        }
    }

    BTreeNode* GetNode(uint64_t id) {
        if (id != 0 && id < nodes.size() && !node_free[id]) return nodes[id].get();
        return nullptr;
    }

    const BTreeNode* GetNode(uint64_t id) const {
        if (id != 0 && id < nodes.size() && !node_free[id]) return nodes[id].get();
        return nullptr;
    }
};

namespace {
int Compare(const Comparator* comparator, const Slice& a, const Slice& b) {
    return comparator ? comparator->Compare(a, b) : a.compare(b);
}

size_t LowerBound(const std::vector<std::string>& keys, const Slice& key, const Comparator* comparator) {
    size_t lo = 0, hi = keys.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (Compare(comparator, Slice(keys[mid]), key) < 0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

// Internal-node separators store the first key of the right child.
// Therefore an exact match must descend to the right child (upper_bound),
// not the left child (lower_bound).
size_t UpperBound(const std::vector<std::string>& keys, const Slice& key, const Comparator* comparator) {
    size_t lo = 0, hi = keys.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (Compare(comparator, Slice(keys[mid]), key) <= 0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

bool KeysStrictlyIncreasing(const std::vector<std::string>& keys, const Comparator* comparator) {
    for (size_t i = 1; i < keys.size(); ++i) {
        if (Compare(comparator, Slice(keys[i - 1]), Slice(keys[i])) >= 0) return false;
    }
    return true;
}
} // namespace

BPlusTree::BPlusTree(const BTreeOptions& options)
    : impl_(std::make_unique<Impl>(options.max_keys_per_node)),
      root_id_(0), root_is_leaf_(true), options_(options), size_(0), node_count_(0), height_(1) {
    if (options_.max_keys_per_node < 3) options_.max_keys_per_node = 3;
    impl_->max_keys_per_node = options_.max_keys_per_node;
    root_id_ = impl_->AllocateNode(true);
    node_count_ = 1;
}

BPlusTree::~BPlusTree() = default;

BPlusTree::BPlusTree(BPlusTree&& other) noexcept
    : impl_(std::move(other.impl_)), root_id_(other.root_id_), root_is_leaf_(other.root_is_leaf_),
      options_(other.options_), size_(other.size_), node_count_(other.node_count_), height_(other.height_) {
    other.root_id_ = 0;
    other.size_ = other.node_count_ = 0;
    other.height_ = 1;
}

BPlusTree& BPlusTree::operator=(BPlusTree&& other) noexcept {
    if (this != &other) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        impl_ = std::move(other.impl_);
        root_id_ = other.root_id_;
        root_is_leaf_ = other.root_is_leaf_;
        options_ = other.options_;
        size_ = other.size_;
        node_count_ = other.node_count_;
        height_ = other.height_;
        other.root_id_ = 0;
        other.size_ = other.node_count_ = 0;
        other.height_ = 1;
    }
    return *this;
}

uint64_t BPlusTree::AllocateNode(bool is_leaf) {
    ++node_count_;
    return impl_->AllocateNode(is_leaf);
}

void BPlusTree::FreeNode(uint64_t node_id) {
    if (impl_->GetNode(node_id)) {
        impl_->FreeNode(node_id);
        if (node_count_ > 0) --node_count_;
    }
}

BTreeNode* BPlusTree::GetNode(uint64_t node_id) { return impl_->GetNode(node_id); }
const BTreeNode* BPlusTree::GetNode(uint64_t node_id) const { return impl_->GetNode(node_id); }

int BPlusTree::BinarySearchKeys(const std::vector<std::string>& keys, const Slice& key) const {
    return static_cast<int>(LowerBound(keys, key, options_.comparator));
}

int BPlusTree::BinarySearchKeys(const std::vector<Slice>& keys, const Slice& key) const {
    size_t lo = 0, hi = keys.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (Compare(options_.comparator, keys[mid], key) < 0) lo = mid + 1;
        else hi = mid;
    }
    return static_cast<int>(lo);
}

uint64_t BPlusTree::FindLeaf(const Slice& key) const {
    uint64_t current = root_id_;
    while (current != 0) {
        const BTreeNode* node = GetNode(current);
        if (!node) return 0;
        if (node->IsLeaf()) return current;
        const auto* internal = static_cast<const InternalNode*>(node);
        size_t idx = UpperBound(internal->keys, key, options_.comparator);
        if (idx >= internal->children.size()) return 0;
        current = internal->children[idx];
    }
    return 0;
}

Status BPlusTree::Put(const Slice& key, const Slice& value) {
    if (key.empty()) return Status::InvalidArgument("Key cannot be empty");
    std::unique_lock<std::shared_mutex> lock(mutex_);

    const size_t max_keys = options_.max_keys_per_node;
    bool inserted = false;

    struct SplitResult { bool split = false; std::string separator; uint64_t right = 0; };

    std::function<Status(uint64_t, SplitResult&)> insert = [&](uint64_t node_id, SplitResult& result) -> Status {
        BTreeNode* node = GetNode(node_id);
        if (!node) return Status::InternalError("Invalid node during insert");

        if (node->IsLeaf()) {
            auto* leaf = static_cast<LeafNode*>(node);
            size_t pos = LowerBound(leaf->keys, key, options_.comparator);
            if (pos < leaf->keys.size() && Compare(options_.comparator, Slice(leaf->keys[pos]), key) == 0) {
                leaf->values[pos] = value.ToString();
                return Status::OK();
            }
            leaf->keys.insert(leaf->keys.begin() + static_cast<std::ptrdiff_t>(pos), key.ToString());
            leaf->values.insert(leaf->values.begin() + static_cast<std::ptrdiff_t>(pos), value.ToString());
            inserted = true;
            if (leaf->keys.size() <= max_keys) return Status::OK();

            uint64_t right_id = AllocateNode(true);
            auto* right = static_cast<LeafNode*>(GetNode(right_id));
            const size_t mid = leaf->keys.size() / 2;
            right->keys.assign(leaf->keys.begin() + static_cast<std::ptrdiff_t>(mid), leaf->keys.end());
            right->values.assign(leaf->values.begin() + static_cast<std::ptrdiff_t>(mid), leaf->values.end());
            leaf->keys.erase(leaf->keys.begin() + static_cast<std::ptrdiff_t>(mid), leaf->keys.end());
            leaf->values.erase(leaf->values.begin() + static_cast<std::ptrdiff_t>(mid), leaf->values.end());
            right->next_leaf = leaf->next_leaf;
            leaf->next_leaf = right_id;
            result.split = true;
            result.separator = right->keys.front();
            result.right = right_id;
            return Status::OK();
        }

        auto* internal = static_cast<InternalNode*>(node);
        size_t child_idx = UpperBound(internal->keys, key, options_.comparator);
        if (child_idx >= internal->children.size()) return Status::InternalError("Invalid child index");
        SplitResult child_result;
        Status s = insert(internal->children[child_idx], child_result);
        if (!s.ok()) return s;
        if (!child_result.split) return Status::OK();

        size_t pos = LowerBound(internal->keys, Slice(child_result.separator), options_.comparator);
        internal->keys.insert(internal->keys.begin() + static_cast<std::ptrdiff_t>(pos), child_result.separator);
        internal->children.insert(internal->children.begin() + static_cast<std::ptrdiff_t>(pos + 1), child_result.right);

        if (internal->keys.size() <= max_keys) return Status::OK();

        uint64_t right_id = AllocateNode(false);
        auto* right = static_cast<InternalNode*>(GetNode(right_id));
        const size_t mid = internal->keys.size() / 2;
        result.separator = internal->keys[mid];
        result.split = true;
        result.right = right_id;
        right->keys.assign(internal->keys.begin() + static_cast<std::ptrdiff_t>(mid + 1), internal->keys.end());
        right->children.assign(internal->children.begin() + static_cast<std::ptrdiff_t>(mid + 1), internal->children.end());
        internal->keys.erase(internal->keys.begin() + static_cast<std::ptrdiff_t>(mid), internal->keys.end());
        internal->children.erase(internal->children.begin() + static_cast<std::ptrdiff_t>(mid + 1), internal->children.end());
        return Status::OK();
    };

    SplitResult result;
    Status s = insert(root_id_, result);
    if (!s.ok()) return s;
    if (inserted) ++size_;

    if (result.split) {
        uint64_t new_root_id = AllocateNode(false);
        auto* root = static_cast<InternalNode*>(GetNode(new_root_id));
        root->keys.push_back(result.separator);
        root->children.push_back(root_id_);
        root->children.push_back(result.right);
        root_id_ = new_root_id;
        root_is_leaf_ = false;
        ++height_;
    }
    return Status::OK();
}

Status BPlusTree::Get(const Slice& key, std::string* value) const {
    if (!value) return Status::InvalidArgument("value output cannot be null");
    std::shared_lock<std::shared_mutex> lock(mutex_);
    uint64_t leaf_id = FindLeaf(key);
    auto* leaf = leaf_id ? static_cast<const LeafNode*>(GetNode(leaf_id)) : nullptr;
    if (!leaf) return Status::InternalError("Leaf not found");
    size_t pos = LowerBound(leaf->keys, key, options_.comparator);
    if (pos < leaf->keys.size() && Compare(options_.comparator, Slice(leaf->keys[pos]), key) == 0) {
        *value = leaf->values[pos];
        return Status::OK();
    }
    return Status::NotFound("Key not found");
}

Status BPlusTree::Delete(const Slice& key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (size_ == 0) return Status::NotFound("Key not found");

    const size_t max_keys = options_.max_keys_per_node;
    struct DeleteResult { bool removed = false; bool underflow = false; std::string first_key; };

    std::function<Status(uint64_t, bool, DeleteResult&)> erase = [&](uint64_t node_id, bool is_root, DeleteResult& out) -> Status {
        BTreeNode* node = GetNode(node_id);
        if (!node) return Status::InternalError("Invalid node during delete");

        if (node->IsLeaf()) {
            auto* leaf = static_cast<LeafNode*>(node);
            size_t pos = LowerBound(leaf->keys, key, options_.comparator);
            if (pos >= leaf->keys.size() || Compare(options_.comparator, Slice(leaf->keys[pos]), key) != 0) {
                return Status::NotFound("Key not found");
            }
            leaf->keys.erase(leaf->keys.begin() + static_cast<std::ptrdiff_t>(pos));
            leaf->values.erase(leaf->values.begin() + static_cast<std::ptrdiff_t>(pos));
            out.removed = true;
            out.first_key = leaf->keys.empty() ? std::string() : leaf->keys.front();
            out.underflow = !is_root && leaf->keys.size() < (max_keys / 2);
            return Status::OK();
        }

        auto* internal = static_cast<InternalNode*>(node);
        size_t idx = UpperBound(internal->keys, key, options_.comparator);
        if (idx >= internal->children.size()) return Status::InternalError("Invalid child index");
        DeleteResult child;
        Status s = erase(internal->children[idx], false, child);
        if (!s.ok()) return s;
        if (!child.removed) return Status::NotFound("Key not found");

        uint64_t child_id = internal->children[idx];
        BTreeNode* child_node = GetNode(child_id);
        if (!child_node) return Status::InternalError("Deleted child missing");

        // A separator is the first key of the right child. A deletion can
        // change that key even when the child does not underflow.
        if (idx > 0 && !child.first_key.empty()) {
            internal->keys[idx - 1] = child.first_key;
        }

        // Return the smallest key in an entire subtree. Internal nodes can be
        // nested several levels deep, so looking only at the first child is not
        // sufficient when refreshing parent separators after deletion.
        std::function<std::string(uint64_t)> first_key_of_subtree = [&](uint64_t id) -> std::string {
            BTreeNode* n = GetNode(id);
            if (!n) return {};
            if (n->IsLeaf()) {
                auto* lf = static_cast<LeafNode*>(n);
                return lf->keys.empty() ? std::string() : lf->keys.front();
            }
            auto* in = static_cast<InternalNode*>(n);
            return in->children.empty() ? std::string() : first_key_of_subtree(in->children.front());
        };

        auto update_separator = [&](size_t child_index) {
            if (child_index > 0 && child_index - 1 < internal->keys.size()) {
                const std::string first = first_key_of_subtree(internal->children[child_index]);
                if (!first.empty()) internal->keys[child_index - 1] = first;
            }
        };

        if (child.underflow) {
            const size_t min_keys = max_keys / 2;
            if (idx > 0) {
                BTreeNode* left = GetNode(internal->children[idx - 1]);
                if (left && left->Size() > min_keys) {
                    if (child_node->IsLeaf()) {
                        auto* l = static_cast<LeafNode*>(left);
                        auto* r = static_cast<LeafNode*>(child_node);
                        r->keys.insert(r->keys.begin(), l->keys.back());
                        r->values.insert(r->values.begin(), l->values.back());
                        l->keys.pop_back(); l->values.pop_back();
                    } else {
                        auto* l = static_cast<InternalNode*>(left);
                        auto* r = static_cast<InternalNode*>(child_node);
                        r->children.insert(r->children.begin(), l->children.back());
                        r->keys.insert(r->keys.begin(), internal->keys[idx - 1]);
                        internal->keys[idx - 1] = l->keys.back();
                        l->children.pop_back(); l->keys.pop_back();
                    }
                    update_separator(idx);
                    child.underflow = false;
                }
            }
            if (child.underflow && idx + 1 < internal->children.size()) {
                BTreeNode* right = GetNode(internal->children[idx + 1]);
                if (right && right->Size() > min_keys) {
                    if (child_node->IsLeaf()) {
                        auto* l = static_cast<LeafNode*>(child_node);
                        auto* r = static_cast<LeafNode*>(right);
                        l->keys.push_back(r->keys.front());
                        l->values.push_back(r->values.front());
                        r->keys.erase(r->keys.begin()); r->values.erase(r->values.begin());
                        internal->keys[idx] = r->keys.front();
                    } else {
                        auto* l = static_cast<InternalNode*>(child_node);
                        auto* r = static_cast<InternalNode*>(right);
                        l->keys.push_back(internal->keys[idx]);
                        l->children.push_back(r->children.front());
                        internal->keys[idx] = r->keys.front();
                        r->keys.erase(r->keys.begin()); r->children.erase(r->children.begin());
                    }
                    child.underflow = false;
                }
            }
            if (child.underflow) {
                if (idx > 0) {
                    BTreeNode* left = GetNode(internal->children[idx - 1]);
                    if (left) {
                        if (child_node->IsLeaf()) {
                            auto* l = static_cast<LeafNode*>(left);
                            auto* r = static_cast<LeafNode*>(child_node);
                            l->keys.insert(l->keys.end(), r->keys.begin(), r->keys.end());
                            l->values.insert(l->values.end(), r->values.begin(), r->values.end());
                            l->next_leaf = r->next_leaf;
                        } else {
                            auto* l = static_cast<InternalNode*>(left);
                            auto* r = static_cast<InternalNode*>(child_node);
                            l->keys.push_back(internal->keys[idx - 1]);
                            l->keys.insert(l->keys.end(), r->keys.begin(), r->keys.end());
                            l->children.insert(l->children.end(), r->children.begin(), r->children.end());
                        }
                        internal->keys.erase(internal->keys.begin() + static_cast<std::ptrdiff_t>(idx - 1));
                        internal->children.erase(internal->children.begin() + static_cast<std::ptrdiff_t>(idx));
                        FreeNode(child_id);
                    }
                } else if (idx + 1 < internal->children.size()) {
                    BTreeNode* right = GetNode(internal->children[idx + 1]);
                    if (right) {
                        if (child_node->IsLeaf()) {
                            auto* l = static_cast<LeafNode*>(child_node);
                            auto* r = static_cast<LeafNode*>(right);
                            l->keys.insert(l->keys.end(), r->keys.begin(), r->keys.end());
                            l->values.insert(l->values.end(), r->values.begin(), r->values.end());
                            l->next_leaf = r->next_leaf;
                        } else {
                            auto* l = static_cast<InternalNode*>(child_node);
                            auto* r = static_cast<InternalNode*>(right);
                            l->keys.push_back(internal->keys[idx]);
                            l->keys.insert(l->keys.end(), r->keys.begin(), r->keys.end());
                            l->children.insert(l->children.end(), r->children.begin(), r->children.end());
                        }
                        const uint64_t right_id = internal->children[idx + 1];
                        internal->keys.erase(internal->keys.begin() + static_cast<std::ptrdiff_t>(idx));
                        internal->children.erase(internal->children.begin() + static_cast<std::ptrdiff_t>(idx + 1));
                        FreeNode(right_id);
                    }
                }
            }
        }

        // Recompute every separator from the minimum key of the right subtree.
        // This is required even when no merge/redistribution occurred: deleting
        // the first key of a child changes its parent's separator.
        for (size_t i = 1; i < internal->children.size(); ++i) {
            const std::string first = first_key_of_subtree(internal->children[i]);
            if (!first.empty()) internal->keys[i - 1] = first;
        }

        out.removed = true;
        // Propagate the minimum key of this whole subtree to its parent.
        out.first_key = internal->children.empty()
            ? std::string()
            : first_key_of_subtree(internal->children.front());
        out.underflow = !is_root && internal->keys.size() < (max_keys / 2);
        return Status::OK();
    };

    DeleteResult result;
    Status s = erase(root_id_, true, result);
    if (!s.ok()) return s;
    if (!result.removed) return Status::NotFound("Key not found");
    --size_;

    // Root collapse.
    if (!root_is_leaf_) {
        auto* root = static_cast<InternalNode*>(GetNode(root_id_));
        if (root && root->children.size() == 1) {
            uint64_t old_root = root_id_;
            root_id_ = root->children.front();
            BTreeNode* new_root = GetNode(root_id_);
            root_is_leaf_ = new_root && new_root->IsLeaf();
            if (height_ > 1) --height_;
            FreeNode(old_root);
        } else if (root && root->children.empty()) {
            FreeNode(root_id_);
            root_id_ = AllocateNode(true);
            root_is_leaf_ = true;
            height_ = 1;
        }
    }
    return Status::OK();
}

Status BPlusTree::InsertIntoLeaf(LeafNode*, const Slice&, const Slice&) { return Status::NotSupported(); }
Status BPlusTree::InsertIntoInternal(InternalNode*, const Slice&, uint64_t) { return Status::NotSupported(); }
void BPlusTree::SplitLeaf(LeafNode*, LeafNode*, std::string*) {}
void BPlusTree::SplitInternal(InternalNode*, InternalNode*, Slice*, uint64_t*) {}
Status BPlusTree::SplitLeafAndInsert(LeafNode*, uint64_t) { return Status::NotSupported(); }
Status BPlusTree::InsertIntoParent(uint64_t, const Slice&, uint64_t) { return Status::NotSupported(); }
Status BPlusTree::SplitInternalAndInsert(InternalNode*, uint64_t) { return Status::NotSupported(); }
Status BPlusTree::DeleteFromLeaf(LeafNode*, const Slice&) { return Status::NotSupported(); }
Status BPlusTree::DeleteFromInternal(InternalNode*, const Slice&) { return Status::NotSupported(); }
void BPlusTree::RebalanceOrMerge(InternalNode*, size_t) {}
Status BPlusTree::DeleteFromLeafWithRebalance(uint64_t, const Slice&) { return Status::NotSupported(); }
Status BPlusTree::DeleteFromInternalWithRebalance(uint64_t, const Slice&) { return Status::NotSupported(); }
void BPlusTree::RebalanceLeaf(uint64_t, InternalNode*, size_t) {}
void BPlusTree::RebalanceInternal(uint64_t, InternalNode*, size_t) {}
void BPlusTree::MergeLeaves(LeafNode*, LeafNode*, InternalNode*, size_t) {}
void BPlusTree::MergeInternals(InternalNode*, InternalNode*, InternalNode*, size_t) {}
void BPlusTree::RedistributeLeaves(LeafNode*, LeafNode*, InternalNode*, size_t, bool) {}
void BPlusTree::RedistributeInternals(InternalNode*, InternalNode*, InternalNode*, size_t, bool) {}
void BPlusTree::UpdateParentSeparator(InternalNode*, size_t, const Slice&) {}

size_t BPlusTree::MemoryUsage() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    size_t usage = sizeof(*this);
    for (const auto& node : impl_->nodes) {
        if (!node) continue;
        usage += sizeof(*node);
        if (node->IsLeaf()) {
            const auto* leaf = static_cast<const LeafNode*>(node.get());
            usage += leaf->keys.capacity() * sizeof(std::string);
            usage += leaf->values.capacity() * sizeof(std::string);
            for (const auto& k : leaf->keys) usage += k.capacity();
            for (const auto& v : leaf->values) usage += v.capacity();
        } else {
            const auto* in = static_cast<const InternalNode*>(node.get());
            usage += in->keys.capacity() * sizeof(std::string);
            usage += in->children.capacity() * sizeof(uint64_t);
            for (const auto& k : in->keys) usage += k.capacity();
        }
    }
    return usage;
}

void BPlusTree::Print() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::cout << "B+ Tree: size=" << size_ << ", nodes=" << node_count_
              << ", height=" << height_ << ", root_is_leaf=" << (root_is_leaf_ ? "true" : "false") << "\n";
    if (root_id_) PrintNode(root_id_, 0);
}

void BPlusTree::PrintNode(uint64_t node_id, int depth) const {
    const BTreeNode* node = GetNode(node_id);
    if (!node) return;
    std::string indent(static_cast<size_t>(depth) * 2, ' ');
    if (node->IsLeaf()) {
        const auto* leaf = static_cast<const LeafNode*>(node);
        std::cout << indent << "Leaf [" << leaf->keys.size() << "]: ";
        for (const auto& key : leaf->keys) std::cout << key << ' ';
        std::cout << "\n";
    } else {
        const auto* internal = static_cast<const InternalNode*>(node);
        std::cout << indent << "Internal [" << internal->keys.size() << "]: ";
        for (const auto& key : internal->keys) std::cout << key << ' ';
        std::cout << "\n";
        for (uint64_t child : internal->children) PrintNode(child, depth + 1);
    }
}

bool BPlusTree::Verify() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (!impl_ || root_id_ == 0 || !GetNode(root_id_)) return false;

    std::vector<uint64_t> leaves;
    size_t counted = 0;
    int expected_leaf_depth = -1;
    bool ok = true;

    std::function<std::string(uint64_t, int, bool)> verify_node = [&](uint64_t id, int depth, bool is_root) -> std::string {
        const BTreeNode* node = GetNode(id);
        if (!node) { ok = false; return {}; }
        if (node->IsLeaf()) {
            const auto* leaf = static_cast<const LeafNode*>(node);
            if (!KeysStrictlyIncreasing(leaf->keys, options_.comparator)) ok = false;
            if (!is_root && leaf->keys.size() < options_.max_keys_per_node / 2) ok = false;
            if (expected_leaf_depth < 0) expected_leaf_depth = depth;
            else if (expected_leaf_depth != depth) ok = false;
            counted += leaf->keys.size();
            leaves.push_back(id);
            return leaf->keys.empty() ? std::string() : leaf->keys.front();
        }
        const auto* in = static_cast<const InternalNode*>(node);
        if (in->children.size() != in->keys.size() + 1 || in->children.empty()) ok = false;
        if (!is_root && in->keys.size() < options_.max_keys_per_node / 2) ok = false;
        if (!KeysStrictlyIncreasing(in->keys, options_.comparator)) ok = false;
        std::vector<std::string> firsts;
        for (size_t i = 0; i < in->children.size(); ++i) firsts.push_back(verify_node(in->children[i], depth + 1, false));
        for (size_t i = 1; i < firsts.size(); ++i) {
            if (!firsts[i].empty() && Compare(options_.comparator, Slice(in->keys[i - 1]), Slice(firsts[i])) != 0) ok = false;
        }
        return firsts.empty() ? std::string() : firsts.front();
    };

    verify_node(root_id_, 0, true);
    if (counted != size_) ok = false;
    if (leaves.size() > 1) {
        uint64_t id = leaves.front();
        size_t visited = 0;
        while (id != 0 && visited <= leaves.size()) {
            auto* leaf = static_cast<const LeafNode*>(GetNode(id));
            if (!leaf) { ok = false; break; }
            if (visited >= leaves.size() || leaves[visited] != id) { ok = false; break; }
            id = leaf->next_leaf;
            ++visited;
        }
        if (visited != leaves.size() || id != 0) ok = false;
    }
    return ok;
}

struct BPlusTree::Iterator::Impl {
    BPlusTree* tree = nullptr;
    std::unique_ptr<std::shared_lock<std::shared_mutex>> read_lock;
    uint64_t current_leaf = 0;
    size_t current_idx = 0;
    bool has_start = false;
    bool has_limit = false;
    std::string start_key;
    std::string limit_key;
    Status status_;

    Impl(BPlusTree* t, const Slice* start, const Slice* limit) : tree(t) {
        read_lock = std::make_unique<std::shared_lock<std::shared_mutex>>(tree->mutex_);
        if (start) { has_start = true; start_key = start->ToString(); }
        if (limit) { has_limit = true; limit_key = limit->ToString(); }
    }

    void FindFirst() {
        Slice start(has_start ? start_key : std::string());
        current_leaf = has_start ? tree->FindLeaf(start) : tree->root_id_;
        if (!has_start) {
            while (current_leaf) {
                const auto* node = tree->GetNode(current_leaf);
                if (!node) { status_ = Status::InternalError("Invalid root"); return; }
                if (node->IsLeaf()) break;
                const auto* in = static_cast<const InternalNode*>(node);
                if (in->children.empty()) { current_leaf = 0; break; }
                current_leaf = in->children.front();
            }
        }
        const auto* leaf = current_leaf ? static_cast<const LeafNode*>(tree->GetNode(current_leaf)) : nullptr;
        if (!leaf) { current_leaf = 0; current_idx = 0; return; }
        current_idx = has_start ? static_cast<size_t>(tree->BinarySearchKeys(leaf->keys, start)) : 0;
        SkipEmptyOrPastLimit();
    }

    void SkipEmptyOrPastLimit() {
        while (current_leaf) {
            const auto* leaf = static_cast<const LeafNode*>(tree->GetNode(current_leaf));
            if (!leaf) { status_ = Status::InternalError("Leaf not found"); return; }
            if (current_idx < leaf->keys.size()) {
                if (!has_limit || Compare(tree->options_.comparator, Slice(leaf->keys[current_idx]), Slice(limit_key)) < 0) return;
            }
            current_leaf = leaf->next_leaf;
            current_idx = 0;
        }
    }

    bool AtEnd() const {
        if (!current_leaf) return true;
        const auto* leaf = static_cast<const LeafNode*>(tree->GetNode(current_leaf));
        if (!leaf || current_idx >= leaf->keys.size()) return true;
        if (has_limit && Compare(tree->options_.comparator, Slice(leaf->keys[current_idx]), Slice(limit_key)) >= 0) return true;
        return false;
    }
};

BPlusTree::Iterator::Iterator(BPlusTree* tree, const Slice* start, const Slice* limit)
    : impl_(std::make_unique<Impl>(tree, start, limit)) { impl_->FindFirst(); }
BPlusTree::Iterator::~Iterator() = default;
BPlusTree::Iterator::Iterator(Iterator&&) noexcept = default;
BPlusTree::Iterator& BPlusTree::Iterator::operator=(Iterator&&) noexcept = default;

bool BPlusTree::Iterator::Valid() const { return impl_ && impl_->status_.ok() && !impl_->AtEnd(); }
void BPlusTree::Iterator::SeekToFirst() { if (impl_) impl_->FindFirst(); }

void BPlusTree::Iterator::SeekToLast() {
    if (!impl_) return;
    uint64_t current = impl_->tree->root_id_;
    while (current) {
        const auto* node = impl_->tree->GetNode(current);
        if (!node) { impl_->status_ = Status::InternalError("Invalid node"); return; }
        if (node->IsLeaf()) break;
        const auto* in = static_cast<const InternalNode*>(node);
        if (in->children.empty()) { current = 0; break; }
        current = in->children.back();
    }
    impl_->current_leaf = current;
    if (current) {
        const auto* leaf = static_cast<const LeafNode*>(impl_->tree->GetNode(current));
        impl_->current_idx = leaf && !leaf->keys.empty() ? leaf->keys.size() - 1 : 0;
    }
}

void BPlusTree::Iterator::Seek(const Slice& target) {
    if (!impl_) return;
    impl_->current_leaf = impl_->tree->FindLeaf(target);
    const auto* leaf = impl_->current_leaf ? static_cast<const LeafNode*>(impl_->tree->GetNode(impl_->current_leaf)) : nullptr;
    impl_->current_idx = leaf ? static_cast<size_t>(impl_->tree->BinarySearchKeys(leaf->keys, target)) : 0;
    impl_->SkipEmptyOrPastLimit();
}

void BPlusTree::Iterator::Next() {
    if (!Valid()) return;
    const auto* leaf = static_cast<const LeafNode*>(impl_->tree->GetNode(impl_->current_leaf));
    if (!leaf) { impl_->status_ = Status::InternalError("Leaf not found"); return; }
    ++impl_->current_idx;
    if (impl_->current_idx >= leaf->keys.size()) {
        impl_->current_leaf = leaf->next_leaf;
        impl_->current_idx = 0;
    }
    impl_->SkipEmptyOrPastLimit();
}

void BPlusTree::Iterator::Prev() { if (impl_) impl_->status_ = Status::NotSupported("Prev is not implemented"); }
Slice BPlusTree::Iterator::key() const {
    if (!Valid()) return Slice();
    const auto* leaf = static_cast<const LeafNode*>(impl_->tree->GetNode(impl_->current_leaf));
    return leaf ? Slice(leaf->keys[impl_->current_idx]) : Slice();
}
Slice BPlusTree::Iterator::value() const {
    if (!Valid()) return Slice();
    const auto* leaf = static_cast<const LeafNode*>(impl_->tree->GetNode(impl_->current_leaf));
    return leaf ? Slice(leaf->values[impl_->current_idx]) : Slice();
}
Status BPlusTree::Iterator::status() const { return impl_ ? impl_->status_ : Status::InternalError("Iterator not initialized"); }

BPlusTree::Iterator* BPlusTree::NewIterator() { return new Iterator(this, nullptr, nullptr); }
BPlusTree::Iterator* BPlusTree::NewIterator(const Slice& start) { return new Iterator(this, &start, nullptr); }
BPlusTree::Iterator* BPlusTree::NewIterator(const Slice& start, const Slice& limit) { return new Iterator(this, &start, &limit); }

} // namespace kv_engine
