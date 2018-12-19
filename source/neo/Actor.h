// ----------------------------------------------------------------------------
//  OgmaNeo
//  Copyright(c) 2016-2018 Ogma Intelligent Systems Corp. All rights reserved.
//
//  This copy of OgmaNeo is licensed to you under the terms described
//  in the OGMANEO_LICENSE.md file included in this distribution.
// ----------------------------------------------------------------------------

#pragma once

#include "ComputeSystem.h"

namespace ogmaneo {
    /*!
    \brief A actor layer (swarm intelligence of actor columns)
    Maps input CSDRs to actions using a swarm of actor-critic units with Boltzmann exploration
    */
    class Actor {
    public:
        /*!
        \brief Visible layer descriptor
        */
        struct VisibleLayerDesc {
            /*!
            \brief Visible layer size
            */
            Int3 _size;

            /*!
            \brief Radius onto hidden layer
            */
            int _radius;

            /*!
            \brief Initialize defaults
            */
            VisibleLayerDesc()
                : _size({ 4, 4, 16 }),
                _radius(2)
            {}
        };

        /*!
        \brief Visible layer
        */
        struct VisibleLayer {
            //!@{
            /*!
            \brief Visible layer values and buffers
            */
            FloatBuffer _valueWeights;
            FloatBuffer _valueWeightsFixed;
            FloatBuffer _actionWeights;

            Float2 _hiddenToVisible;
            //!@}
        };

        /*!
        \brief History sample for delayed updating
        */
        struct HistorySample {
            std::vector<std::shared_ptr<IntBuffer>> _visibleCs;
            std::shared_ptr<IntBuffer> _hiddenCs;
            std::shared_ptr<FloatBuffer> _hiddenValues;
            
            float _reward;
        };

    private:
        /*!
        \brief Size of the hidden layer (output/action size)
        */
        Int3 _hiddenSize;

        /*!
        \brief Current history size - fixed after initialization. Determines length of wait before updating value function
        */
        int _historySize;

        //!@{
        /*!
        \brief Buffers for state
        */
        IntBuffer _hiddenCs;

        FloatBuffer _hiddenValues;

        std::vector<HistorySample> _historySamples;
        //!@}

        //!@{
        /*!
        \brief Visible layers and associated descriptors
        */
        std::vector<VisibleLayer> _visibleLayers;
        std::vector<VisibleLayerDesc> _visibleLayerDescs;
        //!@}

        //!@{
        /*!
        \brief Kernels
        */
        void init(int pos, std::mt19937 &rng, int vli);
        void forward(const Int2 &pos, std::mt19937 &rng, const std::vector<const IntBuffer*> &inputCs);
        void learn(const Int2 &pos, std::mt19937 &rng, const std::vector<const IntBuffer*> &inputCsPrev, const IntBuffer* hiddenCsPrev, const FloatBuffer* hiddenValuesPrev, float q, float g);

        static void initKernel(int pos, std::mt19937 &rng, Actor* a, int vli) {
            a->init(pos, rng, vli);
        }

        static void forwardKernel(const Int2 &pos, std::mt19937 &rng, Actor* a, const std::vector<const IntBuffer*> &inputCs) {
            a->forward(pos, rng, inputCs);
        }

        static void learnKernel(const Int2 &pos, std::mt19937 &rng, Actor* a, const std::vector<const IntBuffer*> &inputCsPrev, const IntBuffer* hiddenCsPrev, const FloatBuffer* hiddenValuesPrev, float q, float g) {
            a->learn(pos, rng, inputCsPrev, hiddenCsPrev, hiddenValuesPrev, q, g);
        }
        //!@}

    public:
        /*!
        \brief Value learning rate
        */
        float _alpha;

        /*!
        \brief Action learning rate
        */
        float _beta;

        /*!
        \brief Discount factor
        */
        float _gamma;

        /*!
        \brief Fixed weight update rate
        */
        float _fixedRate;

        /*!
        \brief Initialize defaults
        */
        Actor()
        : _alpha(0.5f), _beta(0.5f), _gamma(0.9f), _fixedRate(0.02f)
        {}

        /*!
        \brief Create an actor layer with random initialization
        \param cs is the ComputeSystem
        \param hiddenSize size of the actions (output)
        \param historyCapacity maximum number of history samples (fixed)
        \param visibleLayerDescs are descriptors for visible layers
        */
        void createRandom(ComputeSystem &cs,
            const Int3 &hiddenSize, int historyCapacity, const std::vector<VisibleLayerDesc> &visibleLayerDescs);

        /*!
        \brief Activate the actor (predict values)
        \param cs is the ComputeSystem
        \param visibleCs the visible (input) layer states
        \param hiddenCs the previously taken actions
        \param reward reinforcement signal
        \param learn whether to learn
        */
        void step(ComputeSystem &cs, const std::vector<const IntBuffer*> &visibleCs, float reward, bool learnEnabled);

        /*!
        \brief Get number of visible layers
        */
        int getNumVisibleLayers() const {
            return _visibleLayers.size();
        }

        /*!
        \brief Get a visible layer
        */
        const VisibleLayer &getVisibleLayer(int index) const {
            return _visibleLayers[index];
        }

        /*!
        \brief Get a visible layer descriptor
        */
        const VisibleLayerDesc &getVisibleLayerDesc(int index) const {
            return _visibleLayerDescs[index];
        }

        /*!
        \brief Get the hidden activations (predictions)
        */
        const IntBuffer &getHiddenCs() const {
            return _hiddenCs;
        }

        /*!
        \brief Get the hidden size
        */
        const Int3 &getHiddenSize() const {
            return _hiddenSize;
        }

        /*!
        \brief Get the weights for a visible layer
        */
        const FloatBuffer &getValueWeights(int v) {
            return _visibleLayers[v]._valueWeights;
        }

        /*!
        \brief Get the weights for a visible layer
        */
        const FloatBuffer &getActionWeights(int v) {
            return _visibleLayers[v]._actionWeights;
        }
    };
}
