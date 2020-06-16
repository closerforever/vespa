// Copyright 2020 Oath Inc. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include <vespa/eval/tensor/dense/typed_cells.h>
#include <vespa/searchlib/common/bitvector.h>
#include <vespa/searchlib/tensor/distance_functions.h>
#include <vespa/searchlib/tensor/doc_vector_access.h>
#include <vespa/searchlib/tensor/hnsw_index.h>
#include <vespa/searchlib/tensor/random_level_generator.h>
#include <vespa/searchlib/tensor/inv_log_level_generator.h>
#include <vespa/vespalib/gtest/gtest.h>
#include <vespa/vespalib/util/generationhandler.h>
#include <vespa/vespalib/data/slime/slime.h>
#include <vespa/vespalib/util/threadstackexecutor.h>
#include <vespa/vespalib/util/blockingthreadstackexecutor.h>
#include <vespa/vespalib/util/gate.h>
#include <vespa/vespalib/util/lambdatask.h>

#include <vector>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <chrono>
#include <cstdlib>

#include <vespa/log/log.h>
LOG_SETUP("stress_hnsw_mt");

using vespalib::GenerationHandler;
using vespalib::MemoryUsage;
using namespace search::tensor;
using namespace vespalib::slime;
using vespalib::Slime;
using search::BitVector;

#define NUM_DIMS 128
#define NUM_POSSIBLE_V 100000
#define NUM_POSSIBLE_DOCS 10000
#define NUM_OPS 50000

class RndGen {
private:
    std::mt19937_64 urng;
    std::uniform_real_distribution<double> uf;
public:
    RndGen() : urng(0x1234deadbeef5678uLL), uf(0.0, 1.0) {}

    double nextUniform() {
        return uf(urng);
    }
};

using ConstVectorRef = vespalib::ConstArrayRef<float>;

struct MallocPointVector {
    float v[NUM_DIMS];
    operator ConstVectorRef() const { return ConstVectorRef(v, NUM_DIMS); }
};
static MallocPointVector *aligned_alloc_pv(size_t num) {
    size_t num_bytes = num * sizeof(MallocPointVector);
    double mega_bytes = num_bytes / (1024.0*1024.0);
    fprintf(stderr, "allocate %.2f MB of vectors\n", mega_bytes);
    char *mem = (char *)malloc(num_bytes + 512);
    mem += 512;
    size_t val = (size_t)mem;
    size_t unalign = val % 512;
    mem -= unalign;
    return reinterpret_cast<MallocPointVector *>(mem);
}

void read_vector_file(MallocPointVector *p) {
    std::string data_set = "sift";
    std::string data_dir = ".";
    char *home = getenv("HOME");
    if (home) {
        data_dir = home;
        data_dir += "/" + data_set;
    }
    std::string fn = data_dir + "/" + data_set + "_base.fvecs";
    int fd = open(fn.c_str(), O_RDONLY);
    if (fd < 0) {
        perror(fn.c_str());
        exit(1);
    }
    int d;
    size_t rv;
    fprintf(stderr, "reading %u vectors from %s\n", NUM_POSSIBLE_V, fn.c_str());
    for (uint32_t i = 0; i < NUM_POSSIBLE_V; ++i) {
        rv = read(fd, &d, 4);
        ASSERT_EQ(rv, 4u);
        ASSERT_EQ(d, NUM_DIMS);
        rv = read(fd, &p[i].v, NUM_DIMS*sizeof(float));
        ASSERT_EQ(rv, sizeof(MallocPointVector));
    }
    close(fd);
    fprintf(stderr, "reading %u vectors OK\n", NUM_POSSIBLE_V);
}

class MyDocVectorStore : public DocVectorAccess {
private:
    using Vector = std::vector<float>;
    std::vector<Vector> _vectors;

public:
    MyDocVectorStore() : _vectors() {}
    MyDocVectorStore& set(uint32_t docid, ConstVectorRef vec) {
        if (docid >= _vectors.size()) {
            _vectors.resize(docid + 1);
        }
        _vectors[docid] = Vector(vec.begin(), vec.end());
        return *this;
    }
    vespalib::tensor::TypedCells get_vector(uint32_t docid) const override {
        ConstVectorRef ref(_vectors[docid]);
        return vespalib::tensor::TypedCells(ref);
    }
};

using FloatSqEuclideanDistance = SquaredEuclideanDistance<float>;
using HnswIndexUP = std::unique_ptr<HnswIndex>;

class Stressor : public ::testing::Test {
private:
    struct LoadedVectors {
        MallocPointVector *pv_storage;
        void load() {
            pv_storage = aligned_alloc_pv(size());
            read_vector_file(pv_storage);
        }
        size_t size() const { return NUM_POSSIBLE_V; }
        vespalib::ConstArrayRef<float> operator[] (size_t i) {
            return pv_storage[i];
        }
    } loaded_vectors;
public:
    BitVector::UP in_progress;
    BitVector::UP existing_ids;
    RndGen rng;
    MyDocVectorStore vectors;
    GenerationHandler gen_handler;
    HnswIndexUP index;
    vespalib::BlockingThreadStackExecutor multi_prepare_workers;
    vespalib::BlockingThreadStackExecutor write_thread;

    using PrepUP = std::unique_ptr<PrepareResult>;
    using ReadGuard = GenerationHandler::Guard;

    struct CompleteAddTask : vespalib::Executor::Task {
        Stressor &parent;
        uint32_t docid;
        ConstVectorRef vec;
        PrepUP prepare_result;

        CompleteAddTask(Stressor &p, uint32_t d, ConstVectorRef v, PrepUP r)
            : parent(p), docid(d), vec(v), prepare_result(std::move(r))
        {}

        void run() override {
            parent.vectors.set(docid, vec);
            parent.index->complete_add_document(docid, std::move(prepare_result));
            parent.existing_ids->setBit(docid);
            parent.commit(docid);
        }
    };

    struct TwoPhaseAddTask  : vespalib::Executor::Task {
        Stressor &parent;
        uint32_t docid;
        ConstVectorRef vec;
        GenerationHandler::Guard read_guard;

        TwoPhaseAddTask(Stressor &p, uint32_t d, ConstVectorRef v, ReadGuard r)
            : parent(p), docid(d), vec(v), read_guard(r)
        {}

        void run() override {
            auto v = vespalib::tensor::TypedCells(vec);
            auto up = parent.index->prepare_add_document(docid, v, read_guard);
            auto task = std::make_unique<CompleteAddTask>(parent, docid, vec, std::move(up));
            auto r = parent.write_thread.execute(std::move(task));
            if (r) {
                fprintf(stderr, "Failed posting complete add task!");
                abort();
            }
        }
    };

    struct CompleteRemoveTask : vespalib::Executor::Task {
        Stressor &parent;
        uint32_t docid;

        CompleteRemoveTask(Stressor &p, uint32_t d)
            : parent(p), docid(d)
        {}

        void run() override {
            parent.index->remove_document(docid);
            parent.existing_ids->clearBit(docid);
            parent.commit(docid);
        }
    };

    struct TwoPhaseRemoveTask : vespalib::Executor::Task {
        Stressor &parent;
        uint32_t docid;
        GenerationHandler::Guard read_guard;

        TwoPhaseRemoveTask(Stressor &p, uint32_t d, ReadGuard r)
            : parent(p), docid(d), read_guard(r)
        {}

        void run() override {
            auto task = std::make_unique<CompleteRemoveTask>(parent, docid);
            auto r = parent.write_thread.execute(std::move(task));
            if (r) {
                fprintf(stderr, "Failed posting complete remove task!");
                abort();
            }
        }
    };

    struct CompleteUpdateTask : vespalib::Executor::Task {
        Stressor &parent;
        uint32_t docid;
        ConstVectorRef vec;
        std::unique_ptr<PrepareResult> prepare_result;

        CompleteUpdateTask(Stressor &p, uint32_t d, ConstVectorRef v, PrepUP r)
            : parent(p), docid(d), vec(v), prepare_result(std::move(r))
        {}

        void run() override {
            parent.index->remove_document(docid);
            parent.vectors.set(docid, vec);
            parent.index->complete_add_document(docid, std::move(prepare_result));
            EXPECT_EQ(parent.existing_ids->testBit(docid), true);
            parent.commit(docid);
        }
    };

    struct TwoPhaseUpdateTask : vespalib::Executor::Task {
        Stressor &parent;
        uint32_t docid;
        ConstVectorRef vec;
        GenerationHandler::Guard read_guard;

        TwoPhaseUpdateTask(Stressor &p, uint32_t d, ConstVectorRef v, ReadGuard r)
            : parent(p), docid(d), vec(v), read_guard(r)
        {}

        void run() override {
            auto v = vespalib::tensor::TypedCells(vec);
            auto up = parent.index->prepare_add_document(docid, v, read_guard);
            EXPECT_EQ(bool(up), true);
            auto task = std::make_unique<CompleteUpdateTask>(parent, docid, vec, std::move(up));
            auto r = parent.write_thread.execute(std::move(task));
            if (r) {
                fprintf(stderr, "Failed posting complete remove task!");
                abort();
            }
        }
    };

    Stressor()
        : loaded_vectors(),
          in_progress(BitVector::create(NUM_POSSIBLE_DOCS)),
          existing_ids(BitVector::create(NUM_POSSIBLE_DOCS)),
          rng(),
          vectors(),
          gen_handler(),
          index(),
          multi_prepare_workers(10, 128*1024, 50),
          write_thread(1, 128*1024, 15)
    {
        loaded_vectors.load();
    }

    ~Stressor() {}

    void init() {
        uint32_t m = 16;
        index = std::make_unique<HnswIndex>(vectors, std::make_unique<FloatSqEuclideanDistance>(),
                                            std::make_unique<InvLogLevelGenerator>(m),
                                            HnswIndex::Config(2*m, m, 200, true));
        vectors.set(NUM_POSSIBLE_DOCS, loaded_vectors[0]);
    }
    size_t get_rnd(size_t size) {
        return rng.nextUniform() * size;
    }
    void add_document(uint32_t docid) {
        size_t vec_num = get_rnd(loaded_vectors.size());
        ConstVectorRef vec = loaded_vectors[vec_num];
        auto guard = take_read_guard();
        auto task = std::make_unique<TwoPhaseAddTask>(*this, docid, vec, guard);
        auto r = multi_prepare_workers.execute(std::move(task));
        ASSERT_EQ(bool(r), false);
    }
    void remove_document(uint32_t docid) {
        auto guard = take_read_guard();
        auto task = std::make_unique<TwoPhaseRemoveTask>(*this, docid, guard);
        auto r = multi_prepare_workers.execute(std::move(task));
        ASSERT_EQ(bool(r), false);
    }
    void update_document(uint32_t docid) {
        size_t vec_num = get_rnd(loaded_vectors.size());
        ConstVectorRef vec = loaded_vectors[vec_num];
        auto guard = take_read_guard();
        auto task = std::make_unique<TwoPhaseUpdateTask>(*this, docid, vec, guard);
        auto r = multi_prepare_workers.execute(std::move(task));
        ASSERT_EQ(bool(r), false);
    }
    void commit(uint32_t docid) {
        index->transfer_hold_lists(gen_handler.getCurrentGeneration());
        gen_handler.incGeneration();
        gen_handler.updateFirstUsedGeneration();
        index->trim_hold_lists(gen_handler.getFirstUsedGeneration());
        in_progress->clearBit(docid);
        // printf("commit: %u\n", docid);
    }
    void gen_operation() {
        do {
            uint32_t docid = get_rnd(NUM_POSSIBLE_DOCS);
            if (in_progress->testBit(docid)) continue;
            in_progress->setBit(docid);
            if (existing_ids->testBit(docid)) {
                if (get_rnd(100) < 70) {
                    // printf("start remove op: %u\n", docid);
                    remove_document(docid);
                } else {
                    // printf("start update op: %u\n", docid);
                    update_document(docid);
                }
            } else {
                // printf("start add op: %u\n", docid);
                add_document(docid);
            }
            return;
        } while (true);
    }
    GenerationHandler::Guard take_read_guard() {
        return gen_handler.takeGuard();
    }
    MemoryUsage memory_usage() const {
        return index->memory_usage();
    }
};


TEST_F(Stressor, stress)
{
    init();
    for (int i = 0; i < NUM_OPS; ++i) {
        gen_operation();
        if (i % 1000 == 0) {
            fprintf(stderr, "generating operations %d / %d\n", i, NUM_OPS);
        }
    }
    fprintf(stderr, "waiting for queued operations...\n");
    multi_prepare_workers.sync();
    write_thread.sync();
    in_progress->invalidateCachedCount();
    EXPECT_EQ(in_progress->countTrueBits(), 0);
    fprintf(stderr, "all done.\n");
}

GTEST_MAIN_RUN_ALL_TESTS()
