#include "result_predict.h"

using namespace cv;
using namespace std;

Mat 
resultPredict(const std::vector<Mat> &x, const std::vector<Cvl> &CLayers, const std::vector<Fcl> &hLayers, const Smr &smr){
 
    int nsamples = x.size();
    // Conv & Pooling
    std::vector<std::vector<Mat> > conved;
    convAndPooling(x, CLayers, conved);
    Mat convolvedX = concatenateMat(conved);

    // full connected layers
    std::vector<Mat> hidden;
    hidden.push_back(convolvedX);
    for(int i = 1; i <= fcConfig.size(); i++){
        Mat tmpacti = hLayers[i - 1].W * hidden[i - 1] + repeat(hLayers[i - 1].b, 1, convolvedX.cols);
//        tmpacti = sigmoid(tmpacti);
        tmpacti = ReLU(tmpacti);
        if(fcConfig[i - 1].DropoutRate < 1.0) tmpacti = tmpacti.mul(fcConfig[i - 1].DropoutRate);
        hidden.push_back(tmpacti);
    }
    Mat M = smr.W * hidden[hidden.size() - 1] + repeat(smr.b, 1, nsamples);
    Mat result = Mat::zeros(1, M.cols, CV_64FC1);

    double minValue, maxValue;
    Point minLoc, maxLoc;
    for(int i = 0; i < M.cols; i++){
        minMaxLoc(M(Rect(i, 0, 1, M.rows)), &minValue, &maxValue, &minLoc, &maxLoc);
        result.ATD(0, i) = (int) maxLoc.y;
    }
    // destructor
    for(int i = 0; i < conved.size(); i++){
        conved[i].clear();
    }
    conved.clear();
    std::vector<std::vector<Mat> >().swap(conved);
    M.release();
    hidden.clear();
    std::vector<Mat>().swap(hidden);
    convolvedX.release();
    return result;
}

Mat 
getProbMatrix(const std::vector<Mat> &x, const std::vector<Cvl> &CLayers, const std::vector<Fcl> &hLayers, const Smr &smr){
 
    int nsamples = x.size();
    // Conv & Pooling
    std::vector<std::vector<Mat> > conved;
    convAndPooling(x, CLayers, conved);
    Mat convolvedX = concatenateMat(conved);

    // full connected layers
    std::vector<Mat> hidden;
    hidden.push_back(convolvedX);
    for(int i = 1; i <= fcConfig.size(); i++){
        Mat tmpacti = hLayers[i - 1].W * hidden[i - 1] + repeat(hLayers[i - 1].b, 1, convolvedX.cols);
//        tmpacti = sigmoid(tmpacti);
        tmpacti = ReLU(tmpacti);
        if(fcConfig[i - 1].DropoutRate < 1.0) tmpacti = tmpacti.mul(fcConfig[i - 1].DropoutRate);
        hidden.push_back(tmpacti);
    }
    Mat M = smr.W * hidden[hidden.size() - 1] + repeat(smr.b, 1, nsamples);
    M -= repeat(reduce(M, 0, CV_REDUCE_MAX), M.rows, 1);
    M = exp(M);
    Mat p = divide(M, repeat(reduce(M, 0, CV_REDUCE_SUM), M.rows, 1));
    // destructor
    for(int i = 0; i < conved.size(); i++){
        conved[i].clear();
    }
    conved.clear();
    std::vector<std::vector<Mat> >().swap(conved);
    hidden.clear();
    std::vector<Mat>().swap(hidden);
    convolvedX.release();
    M.release();
    return p;
}

void 
labelJudgement(const Mat &m, std::vector<string> &re_resolmap, std::vector<string> &labels){
    labels.clear();
    if(m.rows == 1){
        std::vector<std::vector<string> > resol;
        std::vector<string> tmpvec;
        for(int i = 0; i < m.cols; i++){
            string str = re_resolmap[m.ATD(0, i)];
            breakString(str, tmpvec);
            resol.push_back(tmpvec);
            tmpvec.clear();
        }
        int length = resol.size() + nGram - 1;
        std::vector<std::vector<string> > vec;
        tmpvec.clear();
        for(int i = 0; i < length; i++) vec.push_back(tmpvec);
        for(int i = 0; i < resol.size(); i++){
            for(int j = 0; j < nGram; j++){
                vec[i + j].push_back(resol[i][j]);
            }
        }
        for(int i = 0; i < length; i++) labels.push_back(getMajoriryElem(vec[i]));
    }else{
        Mat _max = Mat::zeros(1, m.cols, CV_64FC1);
        Mat _prob = Mat::zeros(1, m.cols, CV_64FC1);
        double minValue, maxValue;
        Point minLoc, maxLoc;
        for(int i = 0; i < m.cols; i++){
            minMaxLoc(m(Rect(i, 0, 1, m.rows)), &minValue, &maxValue, &minLoc, &maxLoc);
            _max.ATD(0, i) = (int) maxLoc.y;
            _prob.ATD(0, i) = m.ATD(maxLoc.y, i);
        }
        // cout<<"max --- "<<_max<<endl;
        // cout<<"prob --- "<<_prob<<endl;
        std::vector<string> tmpvec;
        int length = m.cols + nGram - 1;
        for(int i = 0; i < length; i++){
            string str;
            double prob_max = (double)INT_MIN;
            string str_max;
            for(int j = i - nGram + 1; j <= i; j++){
                if(j < 0) continue; 
                if(j >= m.cols) break;
                str = re_resolmap[_max.ATD(0, j)];
                breakString(str, tmpvec);
                if(_prob.ATD(0, j) > prob_max){
                    prob_max = _prob.ATD(0, j);
                    str_max = tmpvec[i - j];
                }
                tmpvec.clear();
            }
            labels.push_back(str_max);
        }
    }
}

void 
sentenceNER(const std::vector<std::string> &sentence, std::vector<Cvl> &CLayers, std::vector<Fcl> &hLayers, Smr &smr, std::unordered_map<string, Mat> &wordvec, std::vector<string> &re_resolmap, bool PROB){

    std::vector<std::vector<string> > resol;
    resolutionerTest(sentence, resol);
    if(resol.empty()){
        cout<<"The amount of word in sentence should be greater than "<<nGram<<"..."<<endl;
        return;
    }
    std::vector<Mat> x;
    for(int i = 0; i < resol.size(); i++){
        x.push_back(vec2Mat(resol[i], wordvec));
    }
    Mat result;
    std::vector<std::string> labels;
    if(PROB) result = getProbMatrix(x, CLayers, hLayers, smr);
    else result = resultPredict(x, CLayers, hLayers, smr);
    labelJudgement(result, re_resolmap, labels);
    for(int i = 0; i < labels.size(); i++){
        cout<<sentence[i]<<" : "<<labels[i]<<endl;
    }
}

void 
sentenceNER(const std::vector<singleWord> &sentence, std::vector<Cvl> &CLayers, std::vector<Fcl> &hLayers, Smr &smr, std::unordered_map<string, Mat> &wordvec, std::vector<string> &re_resolmap, bool PROB){

    std::vector<std::vector<string> > resol;
    resolutionerTest(sentence, resol);
    if(resol.empty()){
        cout<<"The amount of word in sentence should be greater than "<<nGram<<"..."<<endl;
        return;
    }
    std::vector<Mat> x;
    for(int i = 0; i < resol.size(); i++){
        x.push_back(vec2Mat(resol[i], wordvec));
    }

    Mat result;
    std::vector<std::string> labels;
    if(PROB) result = getProbMatrix(x, CLayers, hLayers, smr);
    else result = resultPredict(x, CLayers, hLayers, smr);
    labelJudgement(result, re_resolmap, labels);
    for(int i = 0; i < labels.size(); i++){
        cout<<sentence[i].word<<" : "<<labels[i]<<", "<<num2label(sentence[i].label)<<endl;
    }
}


void 
testNetwork(const std::vector<std::vector<singleWord> > &testData, std::vector<Cvl> &CLayers, std::vector<Fcl> &hLayers, Smr &smr, std::unordered_map<string, Mat> &wordvec, std::vector<string> &re_resolmap, bool PROB){

    int correct = 0;
    int total = 0;
    std::vector<std::vector<string> > resol;

    for(int s = 0; s < testData.size(); s++){
        resol.clear();
        resolutionerTest(testData[s], resol);
        if(resol.empty()) continue;
        std::vector<Mat> x;
        for(int i = 0; i < resol.size(); i++){
            x.push_back(vec2Mat(resol[i], wordvec));
        }
        Mat result;
        std::vector<std::string> labels;
        if(PROB) result = getProbMatrix(x, CLayers, hLayers, smr);
        else result = resultPredict(x, CLayers, hLayers, smr);
        labelJudgement(result, re_resolmap, labels);
        for(int i = 0; i < labels.size(); i++){
            if(labels[i].compare(num2label(testData[s][i].label)) == 0) ++correct;
        }
        total += labels.size();
    }
    resol.clear();
    cout<<"######################################"<<endl;
    cout<<"## CNN - Single word test result. "<<correct<<" correct of "<<total<<" total."<<endl;
    cout<<"## Accuracy is "<<(double)correct / (double)total<<endl;
    cout<<"######################################"<<endl<<endl;
}

void 
testNetwork(const std::vector<std::vector<std::string> > &testX, const Mat &testY, std::vector<Cvl> &CLayers, std::vector<Fcl> &hLayers, Smr &smr, std::unordered_map<string, Mat> &wordvec){

    // Test use test set
    // Because it may leads to lack of memory if testing the whole dataset at 
    // one time, so separate the dataset into small pieces of batches (say, batch size = 20).
    // 
    int batchSize = 20;
    Mat result = Mat::zeros(1, testX.size(), CV_64FC1);
    std::vector<Mat> tmpBatch;
    int batch_amount = testX.size() / batchSize;
    for(int i = 0; i < batch_amount; i++){
//        cout<<"processing batch No. "<<i<<endl;
        for(int j = 0; j < batchSize; j++){
            tmpBatch.push_back(vec2Mat(testX[i * batchSize + j], wordvec));
        }
        Mat resultBatch = resultPredict(tmpBatch, CLayers, hLayers, smr);
        Rect roi = Rect(i * batchSize, 0, batchSize, 1);
        resultBatch.copyTo(result(roi));
        tmpBatch.clear();
    }
    if(testX.size() % batchSize){
//        cout<<"processing batch No. "<<batch_amount<<endl;
        for(int j = 0; j < testX.size() % batchSize; j++){
            tmpBatch.push_back(vec2Mat(testX[batch_amount * batchSize + j], wordvec));
        }
        Mat resultBatch = resultPredict(tmpBatch, CLayers, hLayers, smr);
        Rect roi = Rect(batch_amount * batchSize, 0, testX.size() % batchSize, 1);
        resultBatch.copyTo(result(roi));
        ++ batch_amount;
        tmpBatch.clear();
    }
    Mat err;
    testY.copyTo(err);
    err -= result;
    int correct = err.cols;
    for(int i=0; i<err.cols; i++){
        if(err.ATD(0, i) != 0) --correct;
    }
    cout<<"######################################"<<endl;
    cout<<"## CNN - N-Gram test result. "<<correct<<" correct of "<<err.cols<<" total."<<endl;
    cout<<"## Accuracy is "<<(double)correct / (double)(err.cols)<<endl;
    cout<<"######################################"<<endl;
    result.release();
    err.release();
}
