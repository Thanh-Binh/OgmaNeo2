// ----------------------------------------------------------------------------
//  OgmaNeo
//  Copyright(c) 2016-2018 Ogma Intelligent Systems Corp. All rights reserved.
//
//  This copy of OgmaNeo is licensed to you under the terms described
//  in the OGMANEO_LICENSE.md file included in this distribution.
// ----------------------------------------------------------------------------

#include "SparseCoder.h"

using namespace ogmaneo;

// Kernels
void SparseCoder::init(int pos, std::mt19937 &rng, int vli) {
    // Initialize weights into uniform range
	std::uniform_real_distribution<float> weightDist(0.99f, 1.0f);

    _visibleLayers[vli]._weights[pos] = weightDist(rng);
}

void SparseCoder::forward(const Int2 &pos, std::mt19937 &rng, const std::vector<const IntBuffer*> &inputCs, bool firstIter) {
    // Cache address calculations
    int dxy = _hiddenSize.x * _hiddenSize.y;
    int dxyz = dxy * _hiddenSize.z;

    // Running max data
    int maxIndex = 0;
    float maxValue = -999999.0f;

    // For each hidden cell
    for (int hc = 0; hc < _hiddenSize.z; hc++) {
        Int3 hiddenPosition(pos.x, pos.y, hc);

        // Partial sum cache value
        int dPartial = hiddenPosition.x + hiddenPosition.y * _hiddenSize.x + hiddenPosition.z * dxy;

        // Accumulator
        float inputActivation = 0.0f;
        float reconActivation = 0.0f;

        // For each visible layer
        for (int vli = 0; vli < _visibleLayers.size(); vli++) {
            VisibleLayer &vl = _visibleLayers[vli];
            VisibleLayerDesc &vld = _visibleLayerDescs[vli];

            Int2 visiblePositionCenter = project(pos, vl._hiddenToVisible);

            // Lower corner
            Int2 fieldLowerBound(visiblePositionCenter.x - vld._radius, visiblePositionCenter.y - vld._radius);

            // Additional addressing dimensions
            int diam = vld._radius * 2 + 1;
            int diam2 = diam * diam;

            // Bounds of receptive field, clamped to input size
            Int2 iterLowerBound(std::max(0, fieldLowerBound.x), std::max(0, fieldLowerBound.y));
            Int2 iterUpperBound(std::min(vld._size.x - 1, visiblePositionCenter.x + vld._radius), std::min(vld._size.y - 1, visiblePositionCenter.y + vld._radius));

            for (int x = iterLowerBound.x; x <= iterUpperBound.x; x++)
                for (int y = iterLowerBound.y; y <= iterUpperBound.y; y++) {
                    Int2 visiblePosition(x, y);

                    int visibleIndex = address2(visiblePosition, vld._size.x);

                    {
                        int visibleC = (*inputCs[vli])[visibleIndex];

                        // Complete the partial address with final value needed
                        int az = visiblePosition.x - fieldLowerBound.x + (visiblePosition.y - fieldLowerBound.y) * diam + visibleC * diam2;

                        inputActivation += vl._weights[dPartial + az * dxyz];
                    }

                    if (!firstIter) {
                        int reconC = vl._reconCs[visibleIndex];

                        // Complete the partial address with final value needed
                        int az = visiblePosition.x - fieldLowerBound.x + (visiblePosition.y - fieldLowerBound.y) * diam + reconC * diam2;

                        reconActivation += vl._weights[dPartial + az * dxyz];
                    }
                }
        }

        int hiddenIndex = address3(hiddenPosition, Int2(_hiddenSize.x, _hiddenSize.y));

        if (firstIter) // Clear to new sum value if is first step
            _hiddenActivations[hiddenIndex] = inputActivation;
        else
            _hiddenActivations[hiddenIndex] += inputActivation - reconActivation;

        // Determine highest cell activation and index
        if (_hiddenActivations[hiddenIndex] > maxValue) {
            maxValue = _hiddenActivations[hiddenIndex];

            maxIndex = hc;
        }
    }

    // Output state
    _hiddenCs[address2(pos, _hiddenSize.x)] = maxIndex;
}

void SparseCoder::backward(const Int2 &pos, std::mt19937 &rng, const std::vector<const IntBuffer*> &inputCs, int vli) {
    VisibleLayer &vl = _visibleLayers[vli];
    VisibleLayerDesc &vld = _visibleLayerDescs[vli];

    float maxValue = -999999.0f;
    int maxIndex = 0;

    for (int vc = 0; vc < vld._size.z; vc++) {
        Int3 visiblePosition(pos.x, pos.y, vc);

        // Project to hidden
        Int2 hiddenPositionCenter = project(pos, vl._visibleToHidden);

        // Additional addressing dimensions
        int diam = vld._radius * 2 + 1;
        int diam2 = diam * diam;

        // Accumulators
        float sum = 0.0f;
        float count = 0.0f;

        // Bounds of receptive field, clamped to input size
        Int2 iterLowerBound(std::max(0, hiddenPositionCenter.x - vl._reverseRadii.x), std::max(0, hiddenPositionCenter.y - vl._reverseRadii.y));
        Int2 iterUpperBound(std::min(_hiddenSize.x - 1, hiddenPositionCenter.x + vl._reverseRadii.x), std::min(_hiddenSize.y - 1, hiddenPositionCenter.y + vl._reverseRadii.y));

        for (int x = iterLowerBound.x; x <= iterUpperBound.x; x++)
            for (int y = iterLowerBound.y; y <= iterUpperBound.y; y++) {
                Int2 hiddenPosition(x, y);

                // Next layer node's receptive field
                Int2 visibleFieldCenter = project(hiddenPosition, vl._hiddenToVisible);

                // Bounds of receptive field, clamped to input size
                Int2 fieldLowerBound(visibleFieldCenter.x - vld._radius, visibleFieldCenter.y - vld._radius);
                Int2 fieldUpperBound(visibleFieldCenter.x + vld._radius + 1, visibleFieldCenter.y + vld._radius + 1);

                // Check for containment
                if (inBounds(pos, fieldLowerBound, fieldUpperBound)) {
                    // Address cannot be easily partially computed here, compute fully (address4)
                    int hiddenC = _hiddenCs[address2(hiddenPosition, _hiddenSize.x)];

                    Int4 wPos(hiddenPosition.x, hiddenPosition.y, hiddenC, visiblePosition.x - fieldLowerBound.x + (visiblePosition.y - fieldLowerBound.y) * diam + visiblePosition.z * diam2);

                    sum += vl._weights[address4(wPos, _hiddenSize)];
                    count += 1.0f;
                }
            }

        sum /= std::max(1.0f, count);

        if (sum > maxValue) {
            maxValue = sum;
            maxIndex = vc;
        }
    }

    // Set normalized reconstruction value
    vl._reconCs[address2(pos, vld._size.x)] = maxIndex;
}

void SparseCoder::learn(const Int2 &pos, std::mt19937 &rng, const std::vector<const IntBuffer*> &inputCs, int vli) {
    VisibleLayer &vl = _visibleLayers[vli];
    VisibleLayerDesc &vld = _visibleLayerDescs[vli];

    int visibleIndex = address2(pos, vld._size.x);

    int inputC = (*inputCs[vli])[visibleIndex];

    // Project to hidden
    Int2 hiddenPositionCenter = project(pos, vl._visibleToHidden);

    int diam = vld._radius * 2 + 1;
    int diam2 = diam * diam;

    for (int vc = 0; vc < vld._size.z; vc++) {
        Int3 visiblePosition(pos.x, pos.y, vc);

        float sum = 0.0f;
        float count = 0.0f;

        Int2 iterLowerBound(std::max(0, hiddenPositionCenter.x - vl._reverseRadii.x), std::max(0, hiddenPositionCenter.y - vl._reverseRadii.y));
        Int2 iterUpperBound(std::min(_hiddenSize.x - 1, hiddenPositionCenter.x + vl._reverseRadii.x), std::min(_hiddenSize.y - 1, hiddenPositionCenter.y + vl._reverseRadii.y));

        for (int x = iterLowerBound.x; x <= iterUpperBound.x; x++)
            for (int y = iterLowerBound.y; y <= iterUpperBound.y; y++) {
                Int2 hiddenPosition(x, y);
                
                // Next layer node's receptive field
                Int2 visibleFieldCenter = project(hiddenPosition, vl._hiddenToVisible);

                // Bounds of receptive field, clamped to input size
                Int2 fieldLowerBound(visibleFieldCenter.x - vld._radius, visibleFieldCenter.y - vld._radius);
                Int2 fieldUpperBound(visibleFieldCenter.x + vld._radius + 1, visibleFieldCenter.y + vld._radius + 1);

                // Check for containment
                if (inBounds(pos, fieldLowerBound, fieldUpperBound)) {
                    // Address cannot be easily partially computed here, compute fully (address4)
                    int hiddenC = _hiddenCs[address2(hiddenPosition, _hiddenSize.x)];

                    Int4 wPos(hiddenPosition.x, hiddenPosition.y, hiddenC, visiblePosition.x - fieldLowerBound.x + (visiblePosition.y - fieldLowerBound.y) * diam + visiblePosition.z * diam2);

                    sum += vl._weights[address4(wPos, _hiddenSize)];
                    count += 1.0f;
                }
            }

        // Weight increment
        float target = (vc == inputC ? 1.0f : 0.0f);

        float delta = _alpha * (target - sum / std::max(1.0f, count));

        for (int x = iterLowerBound.x; x <= iterUpperBound.x; x++)
            for (int y = iterLowerBound.y; y <= iterUpperBound.y; y++) {
                Int2 hiddenPosition(x, y);

                // Next layer node's receptive field
                Int2 visibleFieldCenter = project(hiddenPosition, vl._hiddenToVisible);

                // Bounds of receptive field, clamped to input size
                Int2 fieldLowerBound(visibleFieldCenter.x - vld._radius, visibleFieldCenter.y - vld._radius);
                Int2 fieldUpperBound(visibleFieldCenter.x + vld._radius + 1, visibleFieldCenter.y + vld._radius + 1);

                // Check for containment
                if (inBounds(pos, fieldLowerBound, fieldUpperBound)) {
                    // Address cannot be easily partially computed here, compute fully (address4)
                    int hiddenC = _hiddenCs[address2(hiddenPosition, _hiddenSize.x)];

                    Int4 wPos(hiddenPosition.x, hiddenPosition.y, hiddenC, visiblePosition.x - fieldLowerBound.x + (visiblePosition.y - fieldLowerBound.y) * diam + visiblePosition.z * diam2);

                    vl._weights[address4(wPos, _hiddenSize)] += delta;
                }
            }
    }
}

void SparseCoder::createRandom(ComputeSystem &cs,
    const Int3 &hiddenSize, const std::vector<VisibleLayerDesc> &visibleLayerDescs)
{
    _visibleLayerDescs = visibleLayerDescs;

    _hiddenSize = hiddenSize;

    _visibleLayers.resize(_visibleLayerDescs.size());

    // Pre-compute dimensions
    int numHiddenColumns = _hiddenSize.x * _hiddenSize.y;
    int numHidden = numHiddenColumns * _hiddenSize.z;

    // Create layers
    for (int vli = 0; vli < _visibleLayers.size(); vli++) {
        VisibleLayer &vl = _visibleLayers[vli];
        VisibleLayerDesc &vld = _visibleLayerDescs[vli];

        int numVisibleColumns = vld._size.x * vld._size.y;
        int numVisible = numVisibleColumns * vld._size.z;

        // Projection constants
        vl._visibleToHidden = Float2(static_cast<float>(_hiddenSize.x) / static_cast<float>(vld._size.x),
            static_cast<float>(_hiddenSize.y) / static_cast<float>(vld._size.y));

        vl._hiddenToVisible = Float2(static_cast<float>(vld._size.x) / static_cast<float>(_hiddenSize.x),
            static_cast<float>(vld._size.y) / static_cast<float>(_hiddenSize.y));

        vl._reverseRadii = Int2(static_cast<int>(std::ceil(vl._visibleToHidden.x * vld._radius) + 1),
            static_cast<int>(std::ceil(vl._visibleToHidden.y * vld._radius) + 1));

        int diam = vld._radius * 2 + 1;

        int numWeightsPerHidden = diam * diam * vld._size.z;

        int weightsSize = numHidden * numWeightsPerHidden;

        // Create weight matrix for this visible layer and initialize randomly
        vl._weights = FloatBuffer(weightsSize);

#ifdef KERNEL_DEBUG
        for (int x = 0; x < weightsSize; x++)
            init(x, cs._rng, vli);
#else
        runKernel1(cs, std::bind(SparseCoder::initKernel, std::placeholders::_1, std::placeholders::_2, this, vli), weightsSize, cs._rng, cs._batchSize1);
#endif

        // Reconstruction buffer
        vl._reconCs = IntBuffer(numVisibleColumns);
    }

    // Hidden Cs
    _hiddenCs = IntBuffer(numHiddenColumns);

#ifdef KERNEL_DEBUG
    for (int x = 0; x < numHiddenColumns; x++)
        fillInt(x, cs._rng, &_hiddenCs, 0);
#else
    runKernel1(cs, std::bind(fillInt, std::placeholders::_1, std::placeholders::_2, &_hiddenCs, 0), numHiddenColumns, cs._rng, cs._batchSize1);
#endif

    // Hidden activations
    _hiddenActivations = FloatBuffer(numHidden);
}

void SparseCoder::activate(ComputeSystem &cs, const std::vector<const IntBuffer*> &visibleCs) {
    int numHiddenColumns = _hiddenSize.x * _hiddenSize.y;
    int numHidden = numHiddenColumns * _hiddenSize.z;

    // Sparse coding iterations: forward, reconstruct, repeat
    for (int it = 0; it < _explainIters; it++) {
        bool firstIter = it == 0;

#ifdef KERNEL_DEBUG
        for (int x = 0; x < _hiddenSize.x; x++)
            for (int y = 0; y < _hiddenSize.y; y++)
                forward(Int2(x, y), cs._rng, visibleCs, firstIter);
#else
        runKernel2(cs, std::bind(SparseCoder::forwardKernel, std::placeholders::_1, std::placeholders::_2, this, visibleCs, firstIter), Int2(_hiddenSize.x, _hiddenSize.y), cs._rng, cs._batchSize2);
#endif

        for (int vli = 0; vli < _visibleLayers.size(); vli++) {
            VisibleLayer &vl = _visibleLayers[vli];
            VisibleLayerDesc &vld = _visibleLayerDescs[vli];

#ifdef KERNEL_DEBUG
            for (int x = 0; x < vld._size.x; x++)
                for (int y = 0; y < vld._size.y; y++)
                    backward(Int2(x, y), cs._rng, visibleCs, vli);
#else
            runKernel2(cs, std::bind(SparseCoder::backwardKernel, std::placeholders::_1, std::placeholders::_2, this, visibleCs, vli), Int2(vld._size.x, vld._size.y), cs._rng, cs._batchSize2);
#endif
        }
    }
}

void SparseCoder::learn(ComputeSystem &cs, const std::vector<const IntBuffer*> &visibleCs) {
    // Final reconstruction + learning
    for (int vli = 0; vli < _visibleLayers.size(); vli++) {
        VisibleLayer &vl = _visibleLayers[vli];
        VisibleLayerDesc &vld = _visibleLayerDescs[vli];

#ifdef KERNEL_DEBUG
        for (int x = 0; x < vld._size.x; x++)
            for (int y = 0; y < vld._size.y; y++)
                learn(Int2(x, y), cs._rng, visibleCs, vli);
#else
        runKernel2(cs, std::bind(SparseCoder::learnKernel, std::placeholders::_1, std::placeholders::_2, this, visibleCs, vli), Int2(vld._size.x, vld._size.y), cs._rng, cs._batchSize2);
#endif
    }
}

void SparseCoder::writeToStream(std::ostream &os) const {
    int numHiddenColumns = _hiddenSize.x * _hiddenSize.y;
    int numHidden = numHiddenColumns * _hiddenSize.z;

    os.write(reinterpret_cast<const char*>(&_hiddenSize), sizeof(Int3));

    os.write(reinterpret_cast<const char*>(&_alpha), sizeof(float));
    os.write(reinterpret_cast<const char*>(&_explainIters), sizeof(int));

    writeBufferToStream(os, &_hiddenCs);

    int numVisibleLayers = _visibleLayers.size();

    os.write(reinterpret_cast<char*>(&numVisibleLayers), sizeof(int));
    
    for (int vli = 0; vli < _visibleLayers.size(); vli++) {
        const VisibleLayer &vl = _visibleLayers[vli];
        const VisibleLayerDesc &vld = _visibleLayerDescs[vli];

        int numVisibleColumns = vld._size.x * vld._size.y;
        int numVisible = numVisibleColumns * vld._size.z;

        os.write(reinterpret_cast<const char*>(&vld), sizeof(VisibleLayerDesc));

        os.write(reinterpret_cast<const char*>(&vl._visibleToHidden), sizeof(Float2));
        os.write(reinterpret_cast<const char*>(&vl._hiddenToVisible), sizeof(Float2));
        os.write(reinterpret_cast<const char*>(&vl._reverseRadii), sizeof(Int2));

        writeBufferToStream(os, &vl._weights);
    }
}

void SparseCoder::readFromStream(std::istream &is) {
    is.read(reinterpret_cast<char*>(&_hiddenSize), sizeof(Int3));

    int numHiddenColumns = _hiddenSize.x * _hiddenSize.y;
    int numHidden = numHiddenColumns * _hiddenSize.z;

    is.read(reinterpret_cast<char*>(&_alpha), sizeof(float));
    is.read(reinterpret_cast<char*>(&_explainIters), sizeof(int));

    readBufferFromStream(is, &_hiddenCs);

    _hiddenActivations = FloatBuffer(numHidden);

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

        is.read(reinterpret_cast<char*>(&vl._visibleToHidden), sizeof(Float2));
        is.read(reinterpret_cast<char*>(&vl._hiddenToVisible), sizeof(Float2));
        is.read(reinterpret_cast<char*>(&vl._reverseRadii), sizeof(Int2));

        readBufferFromStream(is, &vl._weights);

        vl._reconCs = IntBuffer(numVisibleColumns);
    }
}