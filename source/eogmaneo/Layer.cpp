// ----------------------------------------------------------------------------
//  EOgmaNeo
//  Copyright(c) 2017-2018 Ogma Intelligent Systems Corp. All rights reserved.
//
//  This copy of EOgmaNeo is licensed to you under the terms described
//  in the EOGMANEO_LICENSE.md file included in this distribution.
// ----------------------------------------------------------------------------

#include "Layer.h"

#include <algorithm>
#include <thread>
#include <future>
#include <iostream>

#include <assert.h>

using namespace eogmaneo;

float eogmaneo::sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

void LayerForwardWorkItem::run() {
	_pLayer->columnForward(_ci);
}

void LayerBackwardWorkItem::run() {
	_pLayer->columnBackward(_ci, _v, _rng);
}

void Layer::columnForward(int ci) {
    int hiddenColumnX = ci % _hiddenWidth;
    int hiddenColumnY = ci / _hiddenWidth;

    int hiddenCellIndexPrev = ci + _hiddenStatesPrev[ci] * _hiddenWidth * _hiddenHeight;

    std::vector<float> columnActivations(_columnSize, 0.0f);

    // Activate feed forward
    for (int v = 0; v < _visibleLayerDescs.size(); v++) {
        float toInputX = static_cast<float>(_visibleLayerDescs[v]._width) / static_cast<float>(_hiddenWidth);
        float toInputY = static_cast<float>(_visibleLayerDescs[v]._height) / static_cast<float>(_hiddenHeight);

        int visibleCenterX = hiddenColumnX * toInputX + 0.5f;
        int visibleCenterY = hiddenColumnY * toInputY + 0.5f;

        int forwardRadius = _visibleLayerDescs[v]._forwardRadius;

        int forwardDiam = forwardRadius * 2 + 1;

        int forwardSize = forwardDiam * forwardDiam;

        int lowerVisibleX = visibleCenterX - forwardRadius;
        int lowerVisibleY = visibleCenterY - forwardRadius;

        for (int dcx = -forwardRadius; dcx <= forwardRadius; dcx++)
            for (int dcy = -forwardRadius; dcy <= forwardRadius; dcy++) {
                int cx = visibleCenterX + dcx;
                int cy = visibleCenterY + dcy;

                if (cx >= 0 && cx < _visibleLayerDescs[v]._width && cy >= 0 && cy < _visibleLayerDescs[v]._height) {
                    int visibleColumnIndex = cx + cy * _visibleLayerDescs[v]._width;

                    int inputIndex = _inputs[v][visibleColumnIndex];
                    int inputIndexPrev = _inputsPrev[v][visibleColumnIndex];

                    if (_learn) {
                        // Input cells
                        for (int c = 0; c < _visibleLayerDescs[v]._columnSize; c++) {
                            int wi = (cx - lowerVisibleX) + (cy - lowerVisibleY) * forwardDiam + c * forwardSize;

                            float d = (c == inputIndexPrev ? 0.0f : -_alpha * _feedForwardWeights[v][hiddenCellIndexPrev][wi]);

                            _feedForwardWeights[v][hiddenCellIndexPrev][wi] += d;
                        }
                    }

                    // Output cells
                    int wi = (cx - lowerVisibleX) + (cy - lowerVisibleY) * forwardDiam + inputIndex * forwardSize;

                    for (int c = 0; c < _columnSize; c++) {
                        int hiddenCellIndex = ci + c * _hiddenWidth * _hiddenHeight;
                        
                        columnActivations[c] += _feedForwardWeights[v][hiddenCellIndex][wi];
                    }
                }
            }
    }

	// Find max element
	int maxCellIndex = 0;

	for (int c = 1; c < _columnSize; c++) {
		if (columnActivations[c] > columnActivations[maxCellIndex])
			maxCellIndex = c;
	}

    _hiddenStates[ci] = maxCellIndex;
}

void Layer::columnBackward(int ci, int v, std::mt19937 &rng) {
    int visibleWidth = _visibleLayerDescs[v]._width;
    int visibleHeight = _visibleLayerDescs[v]._height;

    int visibleColumnX = ci % visibleWidth;
    int visibleColumnY = ci / visibleWidth;

    int visibleColumnSize = _visibleLayerDescs[v]._columnSize;
    
    int backwardRadius = _visibleLayerDescs[v]._backwardRadius;

    int backwardDiam = backwardRadius * 2 + 1;
    int backwardSize = backwardDiam * backwardDiam;
    int backwardVecSize = backwardSize * _columnSize;

    float toInputX = static_cast<float>(_hiddenWidth) / static_cast<float>(visibleWidth);
    float toInputY = static_cast<float>(_hiddenHeight) / static_cast<float>(visibleHeight);

    int hiddenCenterX = visibleColumnX * toInputX + 0.5f;
    int hiddenCenterY = visibleColumnY * toInputY + 0.5f;

    int lowerHiddenX = hiddenCenterX - backwardRadius;
    int lowerHiddenY = hiddenCenterY - backwardRadius;

    std::vector<float> columnActivations(visibleColumnSize, 0.0f);
    float count = 0.0f;

    for (int dcx = -backwardRadius; dcx <= backwardRadius; dcx++)
        for (int dcy = -backwardRadius; dcy <= backwardRadius; dcy++) {
            int cx = hiddenCenterX + dcx;
            int cy = hiddenCenterY + dcy;

            if (cx >= 0 && cx < _hiddenWidth && cy >= 0 && cy < _hiddenHeight) {
                int hiddenColumnIndex = cx + cy * _hiddenWidth;

                if (!_feedBack.empty()) {
                    int feedBackIndex = _feedBack[hiddenColumnIndex];

                    // Output cells
                    int wiCur = (cx - lowerHiddenX) + (cy - lowerHiddenY) * backwardDiam + feedBackIndex * backwardSize;

                    for (int c = 0; c < visibleColumnSize; c++) {
                        int visibleCellIndex = ci + c * visibleWidth * visibleHeight;
                            
                        columnActivations[c] += _feedBackWeights[v][visibleCellIndex][wiCur];
                    }

                    count += 1.0f;
                }

                int hiddenIndex = _hiddenStates[hiddenColumnIndex];

                int wiCur = (cx - lowerHiddenX) + (cy - lowerHiddenY) * backwardDiam + hiddenIndex * backwardSize + backwardVecSize;

                // Output cells
                for (int c = 0; c < visibleColumnSize; c++) {
                    int visibleCellIndex = ci + c * visibleWidth * visibleHeight;
        
                    columnActivations[c] += _feedBackWeights[v][visibleCellIndex][wiCur];
                }

                count += 1.0f;
            }
        }

    float rescale = 1.0f / std::max(1.0f, count);

    int predIndex = 0;

    for (int c = 0; c < visibleColumnSize; c++) {
        columnActivations[c] *= rescale;

        if (columnActivations[c] > columnActivations[predIndex])
            predIndex = c;
    }

    _predictions[v][ci] = predIndex;

    if (_historySamples.size() == _valueHorizon && _learn) {
        // Reward sum
        float q = columnActivations[predIndex];
        
        for (int t = 0; t < _valueHorizon - 1; t++) {
            const HistorySample &s = _historySamples[t];
            
            q = s._reward + _gamma * q;
        }

        const HistorySample &s = _historySamples[_valueHorizon - 2];
        const HistorySample &sPrev = _historySamples[_valueHorizon - 1];

        float sColumnActivationPrev = 0.0f;

        int updateIndex = s._inputs[v][ci];

        int visibleCellIndexUpdate = ci + updateIndex * visibleWidth * visibleHeight;

        for (int dcx = -backwardRadius; dcx <= backwardRadius; dcx++)
            for (int dcy = -backwardRadius; dcy <= backwardRadius; dcy++) {
                int cx = hiddenCenterX + dcx;
                int cy = hiddenCenterY + dcy;

                if (cx >= 0 && cx < _hiddenWidth && cy >= 0 && cy < _hiddenHeight) {
                    int hiddenColumnIndex = cx + cy * _hiddenWidth;

                    if (!sPrev._feedBack.empty()) {
                        int feedBackIndexPrev = sPrev._feedBack[hiddenColumnIndex];

                        // Output cells
                        int wiPrev = (cx - lowerHiddenX) + (cy - lowerHiddenY) * backwardDiam + feedBackIndexPrev * backwardSize;

                        sColumnActivationPrev += _feedBackWeights[v][visibleCellIndexUpdate][wiPrev];
                    }

                    int hiddenIndexPrev = sPrev._hiddenStates[hiddenColumnIndex];

                    int wiPrev = (cx - lowerHiddenX) + (cy - lowerHiddenY) * backwardDiam + hiddenIndexPrev * backwardSize + backwardVecSize;

                    // Output cells
                    sColumnActivationPrev += _feedBackWeights[v][visibleCellIndexUpdate][wiPrev];
                }
            }

        sColumnActivationPrev *= rescale;

        // Learn
        float update = _beta * (q - sColumnActivationPrev);
        
        for (int dcx = -backwardRadius; dcx <= backwardRadius; dcx++)
            for (int dcy = -backwardRadius; dcy <= backwardRadius; dcy++) {
                int cx = hiddenCenterX + dcx;
                int cy = hiddenCenterY + dcy;

                if (cx >= 0 && cx < _hiddenWidth && cy >= 0 && cy < _hiddenHeight) {
                    int hiddenColumnIndex = cx + cy * _hiddenWidth;

                    if (!sPrev._feedBack.empty()) {
                        int feedBackIndexPrev = sPrev._feedBack[hiddenColumnIndex];

                        // Output cells
                        int wiPrev = (cx - lowerHiddenX) + (cy - lowerHiddenY) * backwardDiam + feedBackIndexPrev * backwardSize;

                        _feedBackWeights[v][visibleCellIndexUpdate][wiPrev] += update;
                    }

                    int hiddenIndexPrev = sPrev._hiddenStates[hiddenColumnIndex];

                    int wiPrev = (cx - lowerHiddenX) + (cy - lowerHiddenY) * backwardDiam + hiddenIndexPrev * backwardSize + backwardVecSize;

                    _feedBackWeights[v][visibleCellIndexUpdate][wiPrev] += update;
                }
            }
    }
}

void Layer::create(int hiddenWidth, int hiddenHeight, int columnSize, const std::vector<VisibleLayerDesc> &visibleLayerDescs, unsigned long seed) {
    std::mt19937 rng(seed);

    _hiddenWidth = hiddenWidth;
    _hiddenHeight = hiddenHeight;
    _columnSize = columnSize;

    _visibleLayerDescs = visibleLayerDescs;

    _feedForwardWeights.resize(_visibleLayerDescs.size());
    _feedBackWeights.resize(_visibleLayerDescs.size());

    _inputs.resize(_visibleLayerDescs.size());

    _hiddenStates.resize(_hiddenWidth * _hiddenHeight, 0);
    
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    std::uniform_real_distribution<float> initWeightDistHigh(0.999f, 1.0f);
    std::uniform_real_distribution<float> initWeightDistLow(-0.001f, 0.001f);

    for (int v = 0; v < _visibleLayerDescs.size(); v++) {
        _inputs[v].resize(_visibleLayerDescs[v]._width * _visibleLayerDescs[v]._height, 0);

        int forwardVecSize = _visibleLayerDescs[v]._forwardRadius * 2 + 1;

        forwardVecSize *= forwardVecSize * _visibleLayerDescs[v]._columnSize;

        _feedForwardWeights[v].resize(_hiddenWidth * _hiddenHeight * _columnSize);

        for (int x = 0; x < _hiddenWidth; x++)
            for (int y = 0; y < _hiddenHeight; y++) {
                std::vector<bool> mask(forwardVecSize);

                for (int j = 0; j < forwardVecSize; j++)
                    mask[j] = dist01(rng) < 0.5f;

                for (int c = 0; c < _columnSize; c++) {
                    int hiddenCellIndex = x + y * _hiddenWidth + c * _hiddenWidth * _hiddenHeight;

                    _feedForwardWeights[v][hiddenCellIndex].resize(forwardVecSize);
                    
                    for (int j = 0; j < forwardVecSize; j++)
                        _feedForwardWeights[v][hiddenCellIndex][j] = mask[j] ? initWeightDistHigh(rng) : 0.0f;
                }
            }

        if (_visibleLayerDescs[v]._predict) {
            _feedBackWeights[v].resize(_visibleLayerDescs[v]._width * _visibleLayerDescs[v]._height * _visibleLayerDescs[v]._columnSize);

            int backwardVecSize = _visibleLayerDescs[v]._backwardRadius * 2 + 1;

            backwardVecSize *= backwardVecSize * _columnSize * 2;

            for (int x = 0; x < _visibleLayerDescs[v]._width; x++)
                for (int y = 0; y < _visibleLayerDescs[v]._height; y++)         
                    for (int c = 0; c < _visibleLayerDescs[v]._columnSize; c++) {
                        int visibleCellIndex = x + y * _visibleLayerDescs[v]._width + c * _visibleLayerDescs[v]._width * _visibleLayerDescs[v]._height;
                        
                        _feedBackWeights[v][visibleCellIndex].resize(backwardVecSize);

                        for (int j = 0; j < backwardVecSize; j++)
                            _feedBackWeights[v][visibleCellIndex][j] = initWeightDistLow(rng);
                    }
        }
    }

    _feedBack = _hiddenStatesPrev = _hiddenStates;

    _predictions = _inputsPrev = _inputs;
}

void Layer::forward(ComputeSystem &cs, const std::vector<std::vector<int>> &inputs, bool learn) {
    _inputsPrev = _inputs;
    _inputs = inputs;

    _learn = learn;

    _hiddenStatesPrev = _hiddenStates;

    for (int ci = 0; ci < _hiddenStates.size(); ci++) {
        std::shared_ptr<LayerForwardWorkItem> item = std::make_shared<LayerForwardWorkItem>();

        item->_pLayer = this;
        item->_ci = ci;

        cs._pool.addItem(item);
    }
    
    cs._pool.wait();
}

void Layer::backward(ComputeSystem &cs, const std::vector<int> &feedBack, float reward, bool learn) {
	_feedBack = feedBack;

    _learn = learn;

    // Add history sample
    HistorySample s;
    s._hiddenStates = _hiddenStates;
    s._feedBack = _feedBack;
    s._inputs = _inputs;
    s._reward = reward;

    _historySamples.insert(_historySamples.begin(), s);

    if (_historySamples.size() > _valueHorizon)
        _historySamples.resize(_valueHorizon);

    std::uniform_int_distribution<int> seedDist(0, 99999);

    for (int v = 0; v < _visibleLayerDescs.size(); v++) {
        if (!_visibleLayerDescs[v]._predict)
            continue;

        for (int ci = 0; ci < _predictions[v].size(); ci++) {
            std::shared_ptr<LayerBackwardWorkItem> item = std::make_shared<LayerBackwardWorkItem>();

            item->_pLayer = this;
            item->_ci = ci;
            item->_v = v;
            item->_rng.seed(seedDist(cs._rng));

            cs._pool.addItem(item);
        }
    }

    cs._pool.wait();
}

void Layer::readFromStream(std::istream &is) {
    // Read header
    is.read(reinterpret_cast<char*>(&_hiddenWidth), sizeof(int));
    is.read(reinterpret_cast<char*>(&_hiddenHeight), sizeof(int));
    is.read(reinterpret_cast<char*>(&_columnSize), sizeof(int));

    // Read hyperparameters
    is.read(reinterpret_cast<char*>(&_alpha), sizeof(float));
    is.read(reinterpret_cast<char*>(&_beta), sizeof(float));
    is.read(reinterpret_cast<char*>(&_gamma), sizeof(float));
    is.read(reinterpret_cast<char*>(&_valueHorizon), sizeof(int));

    int numVisibleLayerDescs;

    is.read(reinterpret_cast<char*>(&numVisibleLayerDescs), sizeof(int));

    _visibleLayerDescs.resize(numVisibleLayerDescs);

    is.read(reinterpret_cast<char*>(_visibleLayerDescs.data()), _visibleLayerDescs.size() * sizeof(VisibleLayerDesc));

    _inputs.resize(_visibleLayerDescs.size());
    _inputsPrev.resize(_visibleLayerDescs.size());
    _predictions.resize(_visibleLayerDescs.size());

    _feedForwardWeights.resize(_visibleLayerDescs.size());
    _feedBackWeights.resize(_visibleLayerDescs.size());
   
    // Hidden data
    _hiddenStates.resize(_hiddenWidth * _hiddenHeight);
    _hiddenStatesPrev.resize(_hiddenStates.size());
    _feedBack.resize(_hiddenStates.size());

    is.read(reinterpret_cast<char*>(_hiddenStates.data()), _hiddenStates.size() * sizeof(int));
    is.read(reinterpret_cast<char*>(_hiddenStatesPrev.data()), _hiddenStatesPrev.size() * sizeof(int));
    is.read(reinterpret_cast<char*>(_feedBack.data()), _feedBack.size() * sizeof(int));

    // If feedback is -1, clear to empty
    if (_feedBack.front() == -1)
        _feedBack.clear();

    for (int v = 0; v < _visibleLayerDescs.size(); v++) {
        // Visible layer data
        _inputs[v].resize(_visibleLayerDescs[v]._width * _visibleLayerDescs[v]._height);
        _inputsPrev[v].resize(_inputs[v].size());
        _predictions[v].resize(_inputs[v].size());
        
        is.read(reinterpret_cast<char*>(_inputs[v].data()), _inputs[v].size() * sizeof(int));
        is.read(reinterpret_cast<char*>(_inputsPrev[v].data()), _inputsPrev[v].size() * sizeof(int));
        is.read(reinterpret_cast<char*>(_predictions[v].data()), _predictions[v].size() * sizeof(int));

        // Forward weights
        int forwardVecSize = _visibleLayerDescs[v]._forwardRadius * 2 + 1;

        forwardVecSize *= forwardVecSize * _visibleLayerDescs[v]._columnSize;

        _feedForwardWeights[v].resize(_hiddenWidth * _hiddenHeight * _columnSize);

        for (int x = 0; x < _hiddenWidth; x++)
            for (int y = 0; y < _hiddenHeight; y++)
                for (int c = 0; c < _columnSize; c++) {
                    int hiddenCellIndex = x + y * _hiddenWidth + c * _hiddenWidth * _hiddenHeight;

                    _feedForwardWeights[v][hiddenCellIndex].resize(forwardVecSize);

                    is.read(reinterpret_cast<char*>(_feedForwardWeights[v][hiddenCellIndex].data()), _feedForwardWeights[v][hiddenCellIndex].size() * sizeof(float));
                }

        // Backward weights
        if (_visibleLayerDescs[v]._predict) {
            _feedBackWeights[v].resize(_visibleLayerDescs[v]._width * _visibleLayerDescs[v]._height * _visibleLayerDescs[v]._columnSize);

            int backwardVecSize = _visibleLayerDescs[v]._backwardRadius * 2 + 1;

            backwardVecSize *= backwardVecSize * _columnSize;

            for (int x = 0; x < _visibleLayerDescs[v]._width; x++)
                for (int y = 0; y < _visibleLayerDescs[v]._height; y++)         
                    for (int c = 0; c < _visibleLayerDescs[v]._columnSize; c++) {
                        int visibleCellIndex = x + y * _visibleLayerDescs[v]._width + c * _visibleLayerDescs[v]._width * _visibleLayerDescs[v]._height;

                        _feedBackWeights[v][visibleCellIndex].resize(backwardVecSize);
                            
                        is.read(reinterpret_cast<char*>(_feedBackWeights[v][visibleCellIndex].data()), _feedBackWeights[v][visibleCellIndex].size() * sizeof(float));
                    }
        }
    }

    // Load history samples
    int numSamples;

    is.read(reinterpret_cast<char*>(&numSamples), sizeof(int));

    _historySamples.resize(numSamples);

    // Write samples
    for (int t = 0; t < _historySamples.size(); t++) {
        HistorySample &s = _historySamples[t];

        s._hiddenStates.resize(_hiddenStates.size());
        
        is.read(reinterpret_cast<char*>(s._hiddenStates.data()), s._hiddenStates.size() * sizeof(int));

        s._feedBack.resize(_hiddenStates.size());

        is.read(reinterpret_cast<char*>(s._feedBack.data()), s._feedBack.size() * sizeof(int));
    
        if (s._feedBack.front() == -1)
            s._feedBack.clear();

        s._inputs.resize(_visibleLayerDescs.size());

        for (int v = 0; v < _visibleLayerDescs.size(); v++) {
            s._inputs[v].resize(_inputs[v].size());

            is.read(reinterpret_cast<char*>(s._inputs[v].data()), s._inputs[v].size() * sizeof(int));
        }

        is.read(reinterpret_cast<char*>(&s._reward), sizeof(float));
    }
}

void Layer::writeToStream(std::ostream &os) {
    // Write header
    os.write(reinterpret_cast<char*>(&_hiddenWidth), sizeof(int));
    os.write(reinterpret_cast<char*>(&_hiddenHeight), sizeof(int));
    os.write(reinterpret_cast<char*>(&_columnSize), sizeof(int));

    // Write hyperparameters
    os.write(reinterpret_cast<char*>(&_alpha), sizeof(float));
    os.write(reinterpret_cast<char*>(&_beta), sizeof(float));
    os.write(reinterpret_cast<char*>(&_gamma), sizeof(float));
    os.write(reinterpret_cast<char*>(&_valueHorizon), sizeof(int));

    int numVisibleLayerDescs = _visibleLayerDescs.size();

    os.write(reinterpret_cast<char*>(&numVisibleLayerDescs), sizeof(int));

    os.write(reinterpret_cast<char*>(_visibleLayerDescs.data()), _visibleLayerDescs.size() * sizeof(VisibleLayerDesc));

    // Hidden data
    os.write(reinterpret_cast<char*>(_hiddenStates.data()), _hiddenStates.size() * sizeof(int));
    os.write(reinterpret_cast<char*>(_hiddenStatesPrev.data()), _hiddenStatesPrev.size() * sizeof(int));

    std::vector<int> writeFeedBack = _feedBack;

    if (writeFeedBack.empty())
        writeFeedBack.resize(_hiddenStates.size(), -1);

    os.write(reinterpret_cast<char*>(writeFeedBack.data()), writeFeedBack.size() * sizeof(int));

    for (int v = 0; v < _visibleLayerDescs.size(); v++) {
        // Visible layer data
        os.write(reinterpret_cast<char*>(_inputs[v].data()), _inputs[v].size() * sizeof(int));
        os.write(reinterpret_cast<char*>(_inputsPrev[v].data()), _inputsPrev[v].size() * sizeof(int));
        os.write(reinterpret_cast<char*>(_predictions[v].data()), _predictions[v].size() * sizeof(int));

        // Forward weights
        for (int x = 0; x < _hiddenWidth; x++)
            for (int y = 0; y < _hiddenHeight; y++)
                for (int c = 0; c < _columnSize; c++) {
                    int hiddenCellIndex = x + y * _hiddenWidth + c * _hiddenWidth * _hiddenHeight;

                    os.write(reinterpret_cast<char*>(_feedForwardWeights[v][hiddenCellIndex].data()), _feedForwardWeights[v][hiddenCellIndex].size() * sizeof(float));
                }

        // Backward weights
        if (_visibleLayerDescs[v]._predict) {
            for (int x = 0; x < _visibleLayerDescs[v]._width; x++)
                for (int y = 0; y < _visibleLayerDescs[v]._height; y++)         
                    for (int c = 0; c < _visibleLayerDescs[v]._columnSize; c++) {
                        int visibleCellIndex = x + y * _visibleLayerDescs[v]._width + c * _visibleLayerDescs[v]._width * _visibleLayerDescs[v]._height;
                            
                        os.write(reinterpret_cast<char*>(_feedBackWeights[v][visibleCellIndex].data()), _feedBackWeights[v][visibleCellIndex].size() * sizeof(float));
                    }
        }
    }

    // Save history samples
    int numSamples = _historySamples.size();

    os.write(reinterpret_cast<char*>(&numSamples), sizeof(int));

    // Write samples
    for (int t = 0; t < _historySamples.size(); t++) {
        HistorySample &s = _historySamples[t];

        os.write(reinterpret_cast<char*>(s._hiddenStates.data()), s._hiddenStates.size() * sizeof(int));
    
        std::vector<int> writeFeedBack = s._feedBack;

        if (writeFeedBack.empty())
            writeFeedBack.resize(s._hiddenStates.size(), -1);

        os.write(reinterpret_cast<char*>(writeFeedBack.data()), writeFeedBack.size() * sizeof(int));

        for (int v = 0; v < _visibleLayerDescs.size(); v++)
            os.write(reinterpret_cast<char*>(s._inputs[v].data()), s._inputs[v].size() * sizeof(int));

        os.write(reinterpret_cast<char*>(&s._reward), sizeof(float));
    }
}