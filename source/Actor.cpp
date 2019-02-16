// ----------------------------------------------------------------------------
//  OgmaNeo
//  Copyright(c) 2016-2018 Ogma Intelligent Systems Corp. All rights reserved.
//
//  This copy of OgmaNeo is licensed to you under the terms described
//  in the OGMANEO_LICENSE.md file included in this distribution.
// ----------------------------------------------------------------------------

#include "Actor.h"

using namespace ogmaneo;

void Actor::forward(
    const Int2 &pos,
    std::mt19937 &rng,
    const std::vector<const IntBuffer*> &inputCs
) {
    int hiddenColumnIndex = address2C(pos, Int2(_hiddenSize.x, _hiddenSize.y));

    int maxIndex = 0;
    float maxActivation = -999999.0f;

    for (int hc = 0; hc < _hiddenSize.z; hc++) {
        int hiddenIndex = address3C(Int3(pos.x, pos.y, hc), _hiddenSize);

        float sum = 0.0f;

        // For each visible layer
        for (int vli = 0; vli < _visibleLayers.size(); vli++) {
            VisibleLayer &vl = _visibleLayers[vli];
            const VisibleLayerDesc &vld = _visibleLayerDescs[vli];

            sum += vl._weights.multiplyOHVs(*inputCs[vli], hiddenIndex, vld._size.z);
        }

        if (sum > maxActivation) {
            maxActivation = sum;

            maxIndex = hc;
        }
    }
    
    _hiddenCs[hiddenColumnIndex] = maxIndex;
}

void Actor::learn(
    const Int2 &pos,
    std::mt19937 &rng,
    const std::vector<const IntBuffer*> &inputCs,
    const IntBuffer* hiddenCsPrev,
    const IntBuffer* feedBackCsPrev,
    const std::vector<const IntBuffer*> &inputCsPrev
) {
    int hiddenColumnIndex = address2C(pos, Int2(_hiddenSize.x, _hiddenSize.y));

    int targetC = (*hiddenCsPrev)[hiddenColumnIndex];

    float maxActivation = -999999.0f;
    float maxActivationPrev = -999999.0f;
    float nextQActionPrev;
    float qActionPrev;

    for (int hc = 0; hc < _hiddenSize.z; hc++) {
        int hiddenIndex = address3C(Int3(pos.x, pos.y, hc), _hiddenSize);

        float sum = 0.0f;
        float sumPrev = 0.0f;

        // For each visible layer
        for (int vli = 0; vli < _visibleLayers.size(); vli++) {
            VisibleLayer &vl = _visibleLayers[vli];
            const VisibleLayerDesc &vld = _visibleLayerDescs[vli];

            sum += vl._weights.multiplyOHVs(*inputCs[vli], hiddenIndex, vld._size.z);
            sumPrev += vl._weights.multiplyOHVs(*inputCsPrev[vli], hiddenIndex, vld._size.z);
        }

        sum /= std::max(1, _hiddenCounts[hiddenColumnIndex]);
        sumPrev /= std::max(1, _hiddenCounts[hiddenColumnIndex]);

        maxActivation = std::max(maxActivation, sum);
        maxActivationPrev = std::max(maxActivationPrev, sumPrev);

        if (hc == targetC) {
            nextQActionPrev = sum;
            qActionPrev = sumPrev;
        }
    }

    int hiddenIndex = address3C(Int3(pos.x, pos.y, targetC), _hiddenSize);

    float reward = (targetC == (*feedBackCsPrev)[hiddenColumnIndex] ? 1.0f : 0.0f);

    float dQ = reward + _gamma * maxActivation - qActionPrev;
    float dAdv = dQ - _gap * (maxActivationPrev - qActionPrev);
    float dPAL = std::max(dAdv, dQ - _gap * (maxActivation - nextQActionPrev));
    float delta = _alpha * dPAL;

    // For each visible layer
    for (int vli = 0; vli < _visibleLayers.size(); vli++) {
        VisibleLayer &vl = _visibleLayers[vli];
        const VisibleLayerDesc &vld = _visibleLayerDescs[vli];

        vl._weights.deltaOHVs(*inputCsPrev[vli], delta, hiddenIndex, vld._size.z);
    }
}

void Actor::initRandom(
    ComputeSystem &cs,
    const Int3 &hiddenSize,
    int historyCapacity,
    const std::vector<VisibleLayerDesc> &visibleLayerDescs
) {
    _visibleLayerDescs = visibleLayerDescs;

    _hiddenSize = hiddenSize;

    _visibleLayers.resize(_visibleLayerDescs.size());

    // Pre-compute dimensions
    int numHiddenColumns = _hiddenSize.x * _hiddenSize.y;
    int numHidden = numHiddenColumns * _hiddenSize.z;

    _hiddenCounts = IntBuffer(numHiddenColumns, 0);

    std::uniform_real_distribution<float> weightDist(-0.0001f, 0.0f);

    // Create layers
    for (int vli = 0; vli < _visibleLayers.size(); vli++) {
        VisibleLayer &vl = _visibleLayers[vli];
        VisibleLayerDesc &vld = _visibleLayerDescs[vli];

        int numVisibleColumns = vld._size.x * vld._size.y;
        int numVisible = numVisibleColumns * vld._size.z;

        // Create weight matrix for this visible layer and initialize randomly
        initSMLocalRF(vld._size, _hiddenSize, vld._radius, vl._weights);

        for (int i = 0; i < vl._weights._nonZeroValues.size(); i++)
            vl._weights._nonZeroValues[i] = weightDist(cs._rng);

        for (int i = 0; i < numHiddenColumns; i++)
            _hiddenCounts[i] += vl._weights.counts(i * _hiddenSize.z) / vld._size.z;
    }

    // Hidden Cs
    _hiddenCs = IntBuffer(numHiddenColumns, 0);

    // Create (pre-allocated) history samples
    _historySize = 0;
    _historySamples.resize(historyCapacity);

    for (int i = 0; i < _historySamples.size(); i++) {
        _historySamples[i] = std::make_shared<HistorySample>();

        _historySamples[i]->_inputCs.resize(_visibleLayers.size());

        for (int vli = 0; vli < _visibleLayers.size(); vli++) {
            VisibleLayerDesc &vld = _visibleLayerDescs[vli];

            int numVisibleColumns = vld._size.x * vld._size.y;

            _historySamples[i]->_inputCs[vli] = IntBuffer(numVisibleColumns);
        }

        _historySamples[i]->_hiddenCs = IntBuffer(numHiddenColumns);
    }
}

const Actor &Actor::operator=(
    const Actor &other
) {
    _hiddenSize = other._hiddenSize;

    _historySize = other._historySize;

    _hiddenCs = other._hiddenCs;

    _hiddenCounts = other._hiddenCounts;

    _visibleLayerDescs = other._visibleLayerDescs;
    _visibleLayers = other._visibleLayers;

    _alpha = other._alpha;
    _gamma = other._gamma;
    _gap = other._gap;
    _historyIters = other._historyIters;

    _historySamples.resize(other._historySamples.size());

    for (int t = 0; t < _historySamples.size(); t++) {
        _historySamples[t] = std::make_shared<HistorySample>();

        (*_historySamples[t]) = (*other._historySamples[t]);
    }

    return *this;
}

void Actor::step(
    ComputeSystem &cs,
    const std::vector<const IntBuffer*> &inputCs,
    const IntBuffer* hiddenCs,
    const IntBuffer* feedBackCs,
    bool learnEnabled
) {
    int numHiddenColumns = _hiddenSize.x * _hiddenSize.y;
    int numHidden = numHiddenColumns * _hiddenSize.z;

    // Forward kernel
#ifdef KERNEL_NOTHREAD
    for (int x = 0; x < _hiddenSize.x; x++)
        for (int y = 0; y < _hiddenSize.y; y++)
            forward(Int2(x, y), cs._rng, inputCs);
#else
    runKernel2(cs, std::bind(Actor::forwardKernel, std::placeholders::_1, std::placeholders::_2, this, inputCs), Int2(_hiddenSize.x, _hiddenSize.y), cs._rng, cs._batchSize2);
#endif

    // Add sample
    if (_historySize == _historySamples.size()) {
        // Circular buffer swap
        std::shared_ptr<HistorySample> temp = _historySamples.front();

        for (int i = 0; i < _historySamples.size() - 1; i++)
            _historySamples[i] = _historySamples[i + 1];

        _historySamples.back() = temp;
    }
    else
        _historySize++;
    
    // Add new sample
    {
        HistorySample &s = *_historySamples[_historySize - 1];

        for (int vli = 0; vli < _visibleLayers.size(); vli++) {
            VisibleLayerDesc &vld = _visibleLayerDescs[vli];

            int numVisibleColumns = vld._size.x * vld._size.y;

            // Copy visible Cs
#ifdef KERNEL_NOTHREAD
            for (int x = 0; x < numVisibleColumns; x++)
                copyInt(x, cs._rng, inputCs[vli], &s._inputCs[vli]);
#else
            runKernel1(cs, std::bind(copyInt, std::placeholders::_1, std::placeholders::_2, inputCs[vli], &s._inputCs[vli]), numVisibleColumns, cs._rng, cs._batchSize1);
#endif
        }

        // Copy hidden Cs
#ifdef KERNEL_NOTHREAD
        for (int x = 0; x < numHiddenColumns; x++)
            copyInt(x, cs._rng, hiddenCs, &s._hiddenCs);
#else
        runKernel1(cs, std::bind(copyInt, std::placeholders::_1, std::placeholders::_2, hiddenCs, &s._hiddenCs), numHiddenColumns, cs._rng, cs._batchSize1);
#endif
        // Copy feed back Cs
#ifdef KERNEL_NOTHREAD
        for (int x = 0; x < numHiddenColumns; x++)
            copyInt(x, cs._rng, feedBackCs, &s._feedBackCs);
#else
        runKernel1(cs, std::bind(copyInt, std::placeholders::_1, std::placeholders::_2, feedBackCs, &s._feedBackCs), numHiddenColumns, cs._rng, cs._batchSize1);
#endif
    }

    // Learn (if have sufficient samples)
    if (learnEnabled && _historySize > 2) {
        std::uniform_int_distribution<int> sampleDist(0, _historySize - 2);

        for (int it = 0; it < _historyIters; it++) {
            int t = sampleDist(cs._rng);

            const HistorySample &s = *_historySamples[t + 1];
            const HistorySample &sPrev = *_historySamples[t];

            // Learn kernel
#ifdef KERNEL_NOTHREAD
            for (int x = 0; x < _hiddenSize.x; x++)
                for (int y = 0; y < _hiddenSize.y; y++)
                    learn(Int2(x, y), cs._rng, constGet(s._inputCs), &s._hiddenCs, &s._feedBackCs, constGet(sPrev._inputCs));
#else
            runKernel2(cs, std::bind(Actor::learnKernel, std::placeholders::_1, std::placeholders::_2, this, constGet(s._inputCs), &s._hiddenCs, &s._feedBackCs, constGet(sPrev._inputCs)), Int2(_hiddenSize.x, _hiddenSize.y), cs._rng, cs._batchSize2);
#endif
        }
    }
}

void Actor::writeToStream(
    std::ostream &os
) const {
    int numHiddenColumns = _hiddenSize.x * _hiddenSize.y;
    int numHidden = numHiddenColumns * _hiddenSize.z;

    os.write(reinterpret_cast<const char*>(&_hiddenSize), sizeof(Int3));

    os.write(reinterpret_cast<const char*>(&_alpha), sizeof(float));
    os.write(reinterpret_cast<const char*>(&_gamma), sizeof(float));
    os.write(reinterpret_cast<const char*>(&_gap), sizeof(float));
    os.write(reinterpret_cast<const char*>(&_historyIters), sizeof(int));

    os.write(reinterpret_cast<const char*>(&_historySize), sizeof(int));

    writeBufferToStream(os, &_hiddenCs);

    writeBufferToStream(os, &_hiddenCounts);

    int numVisibleLayers = _visibleLayers.size();

    os.write(reinterpret_cast<char*>(&numVisibleLayers), sizeof(int));
    
    for (int vli = 0; vli < _visibleLayers.size(); vli++) {
        const VisibleLayer &vl = _visibleLayers[vli];
        const VisibleLayerDesc &vld = _visibleLayerDescs[vli];

        int numVisibleColumns = vld._size.x * vld._size.y;
        int numVisible = numVisibleColumns * vld._size.z;

        os.write(reinterpret_cast<const char*>(&vld), sizeof(VisibleLayerDesc));

        writeSMToStream(os, vl._weights);
    }

    int numHistorySamples = _historySamples.size();

    os.write(reinterpret_cast<const char*>(&numHistorySamples), sizeof(int));

    for (int t = 0; t < _historySamples.size(); t++) {
        const HistorySample &s = *_historySamples[t];

        for (int vli = 0; vli < _visibleLayers.size(); vli++)
            writeBufferToStream(os, &s._inputCs[vli]);

        writeBufferToStream(os, &s._hiddenCs);
        writeBufferToStream(os, &s._feedBackCs);
    }
}

void Actor::readFromStream(
    std::istream &is
) {
    is.read(reinterpret_cast<char*>(&_hiddenSize), sizeof(Int3));

    int numHiddenColumns = _hiddenSize.x * _hiddenSize.y;
    int numHidden = numHiddenColumns * _hiddenSize.z;

    is.read(reinterpret_cast<char*>(&_alpha), sizeof(float));
    is.read(reinterpret_cast<char*>(&_gamma), sizeof(float));
    is.read(reinterpret_cast<char*>(&_gap), sizeof(float));
    is.read(reinterpret_cast<char*>(&_historyIters), sizeof(int));

    is.read(reinterpret_cast<char*>(&_historySize), sizeof(int));

    readBufferFromStream(is, &_hiddenCs);

    readBufferFromStream(is, &_hiddenCounts);

    int numVisibleLayers;
    
    is.read(reinterpret_cast<char*>(&numVisibleLayers), sizeof(int));

    _visibleLayers.resize(numVisibleLayers);
    _visibleLayerDescs.resize(numVisibleLayers);
    
    for (int vli = 0; vli < _visibleLayers.size(); vli++) {
        VisibleLayer &vl = _visibleLayers[vli];
        VisibleLayerDesc &vld = _visibleLayerDescs[vli];

        is.read(reinterpret_cast<char*>(&vld), sizeof(VisibleLayerDesc));

        int numVisibleColumns = vld._size.x * vld._size.y;
        int numVisible = numVisibleColumns * vld._size.z;

        readSMFromStream(is, vl._weights);
    }

    int numHistorySamples;

    is.read(reinterpret_cast<char*>(&numHistorySamples), sizeof(int));

    _historySamples.resize(numHistorySamples);

    for (int t = 0; t < _historySamples.size(); t++) {
        _historySamples[t] = std::make_shared<HistorySample>();

        HistorySample &s = *_historySamples[t];

        s._inputCs.resize(_visibleLayers.size());

        for (int vli = 0; vli < _visibleLayers.size(); vli++)
            readBufferFromStream(is, &s._inputCs[vli]);

        readBufferFromStream(is, &s._hiddenCs);
        readBufferFromStream(is, &s._feedBackCs);
    }
}