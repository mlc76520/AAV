#ifndef AVERAGE_VALUE_H
#define AVERAGE_VALUE_H

#include <Arduino.h>

template<class T>
class AverageValue {

public:
	AverageValue(uint32_t valueNum);
	~AverageValue();
	void push(T value);
	T average();
protected:

private:
	T *values;
	uint32_t valueNum;
	uint32_t currentPosition = 0;
};

template<class T>
AverageValue<T>::AverageValue(uint32_t valueNum) {
	this->valueNum = valueNum;
	this->values = (T *) malloc(sizeof(T) * valueNum);
	this->currentPosition = 0;
	for (uint32_t i = 0; i < this->valueNum; i++) {
		this->values[i] = 0;
	}
}

template<class T>
AverageValue<T>::~AverageValue() {
	free(this->values);
}

template<class T>
void AverageValue<T>::push(T value) {
	this->values[this->currentPosition] = value;
	this->currentPosition = (this->currentPosition + 1) % this->valueNum;
}

template<class T>
T AverageValue<T>::average() {
	T sum = 0;
	for (uint32_t i = 0; i < this->valueNum; i++) {
		sum += this->values[i];
	}
	return (sum / (T)this->valueNum);
}

#endif
