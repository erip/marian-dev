#pragma once

#include "common/options.h"
#include "graph/expression_graph.h"
#include "optimizers/clippers.h"
#include "tensors/backend.h"
#include "tensors/tensor.h"
#include "training/training_state.h"

#include <algorithm>
#include <map>
#include <memory>

namespace marian {

/**
 * Base class for optimizers.
 */
class OptimizerBase : public TrainingObserver {
public:
  OptimizerBase(float eta, float costScale = 1.f, Ptr<ClipperBase> clipper = nullptr)
      : eta_(eta), costScale_(costScale), clipper_(clipper) { }

  void update(Ptr<ExpressionGraph> graph) {
    Tensor p = graph->params()->vals();
    Tensor g = graph->params()->grads();

    update(p, g);
  }

  void update(Tensor params, Tensor grads) {
    if(clipper_)
      clipper_->clip(grads); // @TODO: handle cost scaling?

    // In case we want to add a multiply factor to our learning rate
    updateImpl(params, grads);
  }

  virtual void init(TrainingState& state) override {
    eta_ = state.eta;
  }
  virtual void actAfterLoaded(TrainingState& state) override {
    eta_ = state.eta;
  }
  virtual void actAfterEpoch(TrainingState& state) override {
    eta_ = state.eta;
    if(state.reset)
      resetStats();
  }
  virtual void actAfterBatches(TrainingState& state) override {
    eta_ = state.eta;
    if(state.reset)
      resetStats();
  }
  virtual void actAfterStalled(TrainingState& state) override {
    eta_ = state.eta;
    if(state.reset)
      resetStats();
  }

  void setParams(const std::vector<float>& params) { parseParams(params); }

  typedef std::function<void(size_t /*localDeviceIndex*/,
                             std::vector<float>::const_iterator /*begin*/,
                             std::vector<float>::const_iterator /*end*/)> ScatterStateSetFunc;
  typedef std::function<std::vector<float>(size_t /*localDeviceIndex*/)> GatherStateGetFunc;

  typedef std::function<void(const std::vector<float>& /*data*/, const ScatterStateSetFunc& /*setFn*/)> ScatterStateFunc;
  typedef std::function<std::vector<float>(const GatherStateGetFunc& /*getFn*/)> GatherStateFunc;

  virtual void load(const std::string& /*name*/,
                    const std::vector<Ptr<OptimizerBase>>& /*opts*/,
                    const std::vector<Ptr<Backend>>& /*backends*/,
                    const ScatterStateFunc& /*scatterFn*/) {}
  virtual void save(const std::string& /*name*/,
                    const std::vector<Ptr<OptimizerBase>>& /*opts*/,
                    const GatherStateFunc& /*gatherFn*/,
                    bool /*isMainProcess*/ = true) {}

protected:
  virtual void updateImpl(Tensor params, Tensor grads) = 0;
  virtual void parseParams(const std::vector<float>& params) = 0;
  virtual void resetStats() = 0;

  // Learning rate
  float eta_;
  // Cost scaling factor
  float costScale_{1.f};
  // Clip gradient norm
  Ptr<ClipperBase> clipper_;
};

/**
 * @brief Stochastic gradient descent optimizer.
 */
class Sgd : public OptimizerBase {
public:
  Sgd(float eta, float costScale, Ptr<ClipperBase> clipper = nullptr)
      : OptimizerBase(eta, costScale, clipper) {}

private:
  void updateImpl(Tensor params, Tensor grads) override;

  virtual void parseParams(const std::vector<float>& /*params*/) override {}
  virtual void resetStats() override {}
};

/**
 * @brief Adagrad optimizer
 *
 * http://www.jmlr.org/papers/volume12/duchi11a/duchi11a.pdf
 */
class Adagrad : public OptimizerBase {
public:
  Adagrad(float eta, float costScale, Ptr<ClipperBase> clipper = nullptr)
      : OptimizerBase(eta, costScale, clipper) {}

  void load(const std::string& name,
            const std::vector<Ptr<OptimizerBase>>& opts,
            const std::vector<Ptr<Backend>>& backends,
            const ScatterStateFunc& scatterFn) override;
  void save(const std::string& name,
            const std::vector<Ptr<OptimizerBase>>& opts,
            const GatherStateFunc& gatherFn,
            bool /*isMainProcess*/ = true) override;

private:
  void updateImpl(Tensor params, Tensor grads) override;
  void resetStats() override;

  void parseParams(const std::vector<float>& params) override {
    if(params.size() > 0)
      eps_ = params[0];
  }

  float eps_ = 1e-8f;
  Ptr<TensorAllocator> alloc_;
  Tensor gt_;
};

/**
 * @brief Adam optimizer
 *
 * https://arxiv.org/pdf/1412.6980v8.pdf
 */
class Adam : public OptimizerBase {
public:
  Adam(float eta, float costScale, Ptr<ClipperBase> clipper = nullptr)
      : OptimizerBase(eta, costScale, clipper), t_(0) {}

  void load(const std::string& name,
            const std::vector<Ptr<OptimizerBase>>& opts,
            const std::vector<Ptr<Backend>>& backends,
            const ScatterStateFunc& scatterFn) override;
  void save(const std::string& name,
            const std::vector<Ptr<OptimizerBase>>& opts,
            const GatherStateFunc& gatherFn,
            bool isMainProcess = true) override;

private:
  void updateImpl(Tensor params, Tensor grads) override;
  void resetStats() override;

  virtual void parseParams(const std::vector<float>& params) override {
    if(params.size() > 0)
      beta1_ = params[0];
    if(params.size() > 1)
      beta2_ = params[1];
    if(params.size() > 2)
      eps_ = params[2];

    // weighted decay for AdamW, to be explored, disabled by default
    if(params.size() > 3)
      w_ = params[3];
  }

  float beta1_ = 0.9f;
  float beta2_ = 0.999f;
  float eps_ = 1e-8f;
  float w_ = 0.0f;
  size_t t_;

  Ptr<TensorAllocator> alloc_;
  Tensor mt_;
  Tensor vt_;

  Tensor pm_;
  Tensor gd_;
};

// @TODO: make optimizer take options and maybe a graph or workspace?
// The current way makes it hard to add new features
template <class Algorithm>
Ptr<OptimizerBase> Optimizer(float eta,
                             float costScale,
                             Ptr<ClipperBase> clipper = nullptr,
                             std::vector<float> params = {}) {
  auto opt = Ptr<OptimizerBase>(new Algorithm(eta, costScale, clipper));
  opt->setParams(params);
  return opt;
}

Ptr<OptimizerBase> Optimizer(Ptr<Options> options);
}  // namespace marian
