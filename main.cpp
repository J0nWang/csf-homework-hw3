/*
 * This program simulates cache behavior with various configurations (LRU and FIFO)
 * CSF Assignment 3: Cache Simulator
 * Jonathan Wang
 * jwang612@jh.edu
 */

#include <cstdint>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using std::cin;
using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::vector;

// struct to represent a cache block
struct Block {
  bool valid;
  bool dirty;
  uint32_t tag;
  // we need to separate arrival and last-access timestamps to distinguish 
  // between both FIFO (use arrivalTime) and LRU (use lastAccessTime).
  uint32_t arrivalTime;    // when the block entered the cache (for FIFO)
  uint32_t lastAccessTime; // time of most recent access (for LRU)

  // setting default values
  Block()
      : valid(false), dirty(false), tag(0), arrivalTime(0), lastAccessTime(0) {}
};

// struct to represent a cache set
struct Set {
  vector<Block> blocks;
  Set(int numBlocks) : blocks(numBlocks) {}
};

// struct to hold cache configuration
struct CacheConfig {
  int numSets;      // number of sets
  int numBlocks;    // blocks per set (associativity)
  int blockSize;    // bytes per block
  bool writeAllocate;
  bool writeThrough; // if false => write-back
  bool useLru;       // true => LRU, false => FIFO

  // calculated values
  int offsetBits;
  int indexBits;
  int tagBits;
};

// struct to hold resulting simulation statistics
struct Stats {
  int totalLoads;
  int totalStores;
  int loadHits;
  int loadMisses;
  int storeHits;
  int storeMisses;
  long long totalCycles; // can grow large, so using the long long type

  Stats()
      : totalLoads(0), totalStores(0), loadHits(0), loadMisses(0),
        storeHits(0), storeMisses(0), totalCycles(0) {}
};

// helper function declarations
static bool parseArguments(int argc, char **argv, CacheConfig &config);
static bool isPowerOfTwo(int n);
static void simulateCache(const CacheConfig &config, Stats &stats);
static void extractAddressParts(uint32_t address, const CacheConfig &config,
                                uint32_t &tag, uint32_t &index);
static int findBlockWithTag(const Set &set, uint32_t tag);
static int findEvictionBlock(const Set &set, bool useLru);
static void touchOnHit(Block &blk, bool useLru, uint32_t &globalTime);
static void installBlock(Block &dst, uint32_t tag, uint32_t &globalTime);
static void handleLoad(vector<Set> &cache, uint32_t address,
                       const CacheConfig &config, Stats &stats,
                       uint32_t &globalTime);
static void handleStore(vector<Set> &cache, uint32_t address,
                        const CacheConfig &config, Stats &stats,
                        uint32_t &globalTime);

int main(int argc, char **argv) {
  CacheConfig config;

  // parse command line arguments & check for invalid parameters
  if (!parseArguments(argc, argv, config)) {
    return 1;
  }

  // run simulation
  Stats stats;
  simulateCache(config, stats);

  // lastly, print results
  cout << "Total loads: " << stats.totalLoads << endl;
  cout << "Total stores: " << stats.totalStores << endl;
  cout << "Load hits: " << stats.loadHits << endl;
  cout << "Load misses: " << stats.loadMisses << endl;
  cout << "Store hits: " << stats.storeHits << endl;
  cout << "Store misses: " << stats.storeMisses << endl;
  cout << "Total cycles: " << stats.totalCycles << endl;
  return 0;
}

// helper functions:

static bool parseArguments(int argc, char **argv, CacheConfig &config) {
  if (argc != 7) {
    cerr << "Error: Expected 6 arguments" << endl; // THIS IS DIFFERENT BUT DON"T CHANGE THIS
    cerr << "Usage key: ./csim <sets> <blocks> <bytes> <write-allocate|no-write-allocate> "
         << "<write-through|write-back> <lru|fifo>" << endl;
    return false;
  }

  // parse numeric parameters (args 1-3)
  try {
    config.numSets = std::stoi(argv[1]);
    config.numBlocks = std::stoi(argv[2]);
    config.blockSize = std::stoi(argv[3]);
  } catch (...) {
    cerr << "Error: Non-integer numeric parameter in parameters 1-3" << endl;
    return false;
  }

  // Validate powers of two and minimum block size
  if (!isPowerOfTwo(config.numSets) || config.numSets <= 0) {
    cerr << "Error: Number of sets must be a positive power of 2" << endl;
    return false;
  }
  if (!isPowerOfTwo(config.numBlocks) || config.numBlocks <= 0) {
    cerr << "Error: Number of blocks must be a positive power of 2" << endl;
    return false;
  }
  if (!isPowerOfTwo(config.blockSize) || config.blockSize < 4) {
    cerr << "Error: Block size must be a power of 2 and at least 4" << endl;
    return false;
  }

  // parse policy strings
  const string writeAllocStr = argv[4];
  const string writeThroughStr = argv[5];
  const string evictionStr = argv[6];

  if (writeAllocStr == "write-allocate") {
    config.writeAllocate = true;
  } else if (writeAllocStr == "no-write-allocate") {
    config.writeAllocate = false;
  } else {
    cerr << "Error: Write allocate must be 'write-allocate' or 'no-write-allocate'" << endl;
    return false;
  }

  if (writeThroughStr == "write-through") {
    config.writeThrough = true;
  } else if (writeThroughStr == "write-back") {
    config.writeThrough = false;
  } else {
    cerr << "Error: Write policy must be 'write-through' or 'write-back'" << endl;
    return false;
  }

  if (evictionStr == "lru") {
    config.useLru = true;
  } else if (evictionStr == "fifo") {
    config.useLru = false;
  } else {
    cerr << "Error: Eviction policy must be 'lru' or 'fifo'" << endl;
    return false;
  }

  // check for invalid combinations
  if (!config.writeAllocate && !config.writeThrough) {
    cerr << "Error: no-write-allocate cannot be combined with write-back" << endl;
    return false;
  }

  // lastly, bit positions
  config.offsetBits = (int)std::log2((double)config.blockSize);
  config.indexBits = (int)std::log2((double)config.numSets);
  config.tagBits = 32 - config.offsetBits - config.indexBits;

  return true;
}

// check if a number is a power of 2 and is positive
static bool isPowerOfTwo(int n) { 
  return n > 0 && (n & (n - 1)) == 0; 
}

// get tag and index from address
static void extractAddressParts(uint32_t address, const CacheConfig &config,
                                uint32_t &tag, uint32_t &index) {
  // remove offset bits
  uint32_t addrWithoutOffset = address >> config.offsetBits;

  // extract index
  uint32_t indexMask = (config.indexBits == 0) ? 0 : ((1u << config.indexBits) - 1u);
  index = addrWithoutOffset & indexMask; // for fully-associative caches, index==0

  // lastly, extract tag
  tag = addrWithoutOffset >> config.indexBits;
}

// find valid block with matching tag in a set (-1 if not found)
static int findBlockWithTag(const Set &set, uint32_t tag) {
  for (size_t i = 0; i < set.blocks.size(); i++) {
    if (set.blocks[i].valid && set.blocks[i].tag == tag) {
      return (int)i;  // different, see if this makes any difference (added the (int))
    }
  }
  return -1;
}

// choose an invalid block if any, otherwise choose a victim depending on policy
static int findEvictionBlock(const Set &set, bool useLru) {
  for (size_t i = 0; i < set.blocks.size(); i++) {
    if (!set.blocks[i].valid) {
      return (int)i;
    }
  }

  // if all blocks valid, find victim block using
  // the block with minimum lastAccessTime (LRU)
  // or minimum arrivalTime (FIFO)
  int victimIndex = 0;
  uint32_t best = useLru ? set.blocks[0].lastAccessTime : set.blocks[0].arrivalTime;
  for (size_t i = 1; i < set.blocks.size(); i++) {
    uint32_t key = useLru ? set.blocks[i].lastAccessTime : set.blocks[i].arrivalTime;
    if (key < best) {
      best = key;
      victimIndex = (int)i;
    }
  }
  return victimIndex;
}

// update lastAccessTime if using LRU policy and a cache block is hit
static void touchOnHit(Block &blk, bool useLru, uint32_t &globalTime) {
  if (useLru) {
    blk.lastAccessTime = globalTime++;
  }
}

static void installBlock(Block &dst, uint32_t tag, uint32_t &globalTime) {
  dst.valid = true;
  dst.tag = tag;
  dst.dirty = false;
  dst.arrivalTime = globalTime;
  dst.lastAccessTime = globalTime;
  globalTime++;
}

// handle a (l)oad operation
static void handleLoad(vector<Set> &cache, uint32_t address,
                       const CacheConfig &config, Stats &stats,
                       uint32_t &globalTime) {
  stats.totalLoads++;

  uint32_t tag, index;
  extractAddressParts(address, config, tag, index);
  Set &set = cache[index];

  int i = findBlockWithTag(set, tag);
  if (i != -1) {
    // then it's a hit
    stats.loadHits++;
    stats.totalCycles += 1;
    touchOnHit(set.blocks[i], config.useLru, globalTime);
    return;
  }

  // it's a miss
  stats.loadMisses++;

  // load from memory (costs 100 cycles per 4-byte block)
  int blocksToTransfer = config.blockSize / 4;
  stats.totalCycles += 1 + 100LL * blocksToTransfer;

  int victim = findEvictionBlock(set, config.useLru);
  // if evicting dirty block in write-back, write to memory first
  if (set.blocks[victim].valid && set.blocks[victim].dirty && !config.writeThrough) {
    stats.totalCycles += 100LL * blocksToTransfer;
  }
  installBlock(set.blocks[victim], tag, globalTime);
}

// handle a (s)tore operation
static void handleStore(vector<Set> &cache, uint32_t address,
                        const CacheConfig &config, Stats &stats,
                        uint32_t &globalTime) {
  stats.totalStores++;

  uint32_t tag, index;
  extractAddressParts(address, config, tag, index);
  Set &set = cache[index];

  int i = findBlockWithTag(set, tag);
  if (i != -1) {
    // then it's a hit
    stats.storeHits++;
    stats.totalCycles += 1;
    touchOnHit(set.blocks[i], config.useLru, globalTime);

    // handle the write policy
    if (config.writeThrough) {
      stats.totalCycles += 100; // write to memory immediately
    } else {
      set.blocks[i].dirty = true; // write-back: mark dirty
    }
    return;
  }

  // it's a miss
  stats.storeMisses++;

  if (config.writeAllocate) {
    // load block into cache
    int blocksToTransfer = config.blockSize / 4;
    stats.totalCycles += 1 + 100LL * blocksToTransfer;

    // find block to replace
    int victim = findEvictionBlock(set, config.useLru);

    // if evicting dirty block in write-back, write to memory
    if (set.blocks[victim].valid && set.blocks[victim].dirty && !config.writeThrough) {
      stats.totalCycles += 100LL * blocksToTransfer; // write-back of victim
    }

    installBlock(set.blocks[victim], tag, globalTime);

    // handle write policy
    if (config.writeThrough) {
      // if write-through, write to memory
      set.blocks[victim].dirty = false;
      stats.totalCycles += 100;
    } else {
      // if write-back, mark as dirty
      set.blocks[victim].dirty = true;
    }
  } else {
    // if no-write-allocate to begin with, just write to memory
    stats.totalCycles += 1 + 100;
  }
}

// main cache simulation function
static void simulateCache(const CacheConfig &config, Stats &stats) {
  // initiailize cache and global time tracker
  vector<Set> cache;
  cache.reserve(config.numSets);
  for (int s = 0; s < config.numSets; s++) {
    cache.emplace_back(config.numBlocks);
  }

  uint32_t globalTime = 0;

  // read and process the trace file
  string line;
  while (std::getline(cin, line)) {
    if (line.empty()) {
      continue;
    }

    std::istringstream iss(line);
    char operation;     // (s)tore or (l)oad
    string addressStr;  // memory address
    int ignored;        // ignoring the third field for this assignement
    
    iss >> operation >> addressStr >> ignored;

    // if there are malformed lines, skip them
    if (!iss || (operation != 'l' && operation != 's')) {
      continue;
    }

    // convert address from hex string to uint32_t
    uint32_t address = 0;
    try {
      address = static_cast<uint32_t>(std::stoul(addressStr, nullptr, 16));
    } catch (...) {
      // again, ignore malformed addresses
      continue;
    }

    if (operation == 'l') {
      handleLoad(cache, address, config, stats, globalTime);
    } else {
      handleStore(cache, address, config, stats, globalTime);
    }
  }
}
