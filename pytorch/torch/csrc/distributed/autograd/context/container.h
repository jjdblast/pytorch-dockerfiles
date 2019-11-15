#pragma once

#include <mutex>
#include <unordered_map>

#include <torch/csrc/distributed/autograd/context/context.h>

namespace torch {
namespace distributed {
namespace autograd {

// Singleton class per worker which is responsible for storing the distributed
// autograd context for each autograd pass and also cleans up data for an
// autograd pass once its done.
//
// Each autograd pass is assinged a unique autograd_context_id and all data for
// that pass (DistAutogradContext) is stored in this container indexed by the
// autograd_context_id. The autograd_context_id itself is a 64 bit globally
// unique id. The first 16 bits is the worker_id and the next 48 bits is an
// auto-incrementing id for each worker.
//
// This container is also responsible for maintaining a globally unique message
// id, which is used to associate send/recv autograd function pairs. The format
// is similar to the autograd_context_id where we have a 64 bit integer with
// first 16 bits being the worker id and next 48 bits are auto-incrementing.
class TORCH_API DistAutogradContainer {
 public:
  // One time initialization of the container.
  static DistAutogradContainer& init(int64_t worker_id);

  // Retrieve the singleton instance of the container, ensures we have
  // initialized the container.
  static DistAutogradContainer& getInstance();

  // Create a new context for a distributed autograd pass.
  const DistAutogradContext& newContext();

  // Clean up resources for a given context_id once the autograd pass is done.
  // Sends RPC to other workers this worker knows about, telling them to clean
  // up their context as well. Throws an exception if the context_id does not
  // exist.
  void releaseContext(int64_t context_id);

  // Releases an autograd context if it is present on this node. Also sends RPC
  // to other workers this worker knows about, telling them to clean up their
  // context. Does nothing if it is not present.
  void releaseContextIfPresent(int64_t context_id);

  // Retrieve the autograd context for a given context_id.
  DistAutogradContext& retrieveContext(int64_t context_id);

  // Retrieves the currently active autograd context for the current thread.
  DistAutogradContext& currentContext();

  // Checks whether or not the current thread has a valid autograd context.
  bool hasValidContext() const;

  // Generate a new autograd_message_id for send/recv autograd functions.
  int64_t newAutogradMessageId();

  // Creates a new autograd context with the provided context_id. If a context
  // already exists with the provided context_id, we just return it.
  // This does not set the current context for the current thread.
  DistAutogradContext& getOrCreateContext(int64_t context_id);

  // Retrieves the maximum possible autograd_context_id/autograd_message_id that
  // can be generated by this worker.
  int64_t getMaxId();

  // Retrieves the worker ID for this node
  rpc::worker_id_t getWorkerId() const;

  // Can set current context id if there is no valid context yet
  void setCurrentContextId(int64_t contextId);

  // Clear current context id
  void clearCurrentContext();

 private:
  DistAutogradContainer();
  ~DistAutogradContainer() = default;

  DistAutogradContainer(const DistAutogradContainer&) = delete;
  DistAutogradContainer& operator=(const DistAutogradContainer&) = delete;
  DistAutogradContainer(DistAutogradContainer&&) = delete;
  DistAutogradContainer& operator=(DistAutogradContainer&&) = delete;

  static DistAutogradContainer& getInstanceInternal();

  // Sends an RPC to the workers that have a context corresponding to passed in
  // context_id. This function should be called with the lock.
  void sendReleaseContextRpc(int64_t context_id);

  // Erase context_id from the autograd context map, and reset the thread local
  // current context id if it corresponds to the passed in context id. This
  // function should be called with the lock.
  void eraseContextIdAndReset(int64_t context_id);

  // Auto incrementing context id used to identify unique autograd passes.
  // Initialized with the first 16 bits being the worker_id.
  int64_t next_context_id_;

  // Unique id to identify a worker in the distributed setting.
  int16_t worker_id_;

  // Map from autograd_context_id to DistAutogradContext.
  std::unordered_map<int64_t, DistAutogradContext> autograd_context_;

  // Whether or not the container has been initialized appropriately.
  bool initialized_;

  // Lock to protect next_context_id_ and autograd_context map. initialized_
  // and worker_id_ are immutable.
  mutable std::mutex autograd_context_lock_;

  // Autograd message id to identify unique send/recv autograd function pairs.
  std::atomic<int64_t> next_autograd_message_id_;

  // Maximum allowed value for autograd_context_id or autograd_message_id.
  int64_t max_id_;
};

} // namespace autograd
} // namespace distributed
} // namespace torch
