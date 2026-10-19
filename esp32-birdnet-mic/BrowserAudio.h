#pragma once
#include <stddef.h>
#include <stdint.h>

void browserAudioBegin();
void browserAudioLoop(bool allowed, uint32_t sampleRate);
void browserAudioStop();
bool browserAudioActive();
void browserAudioPush(const int16_t* samples, size_t count);
