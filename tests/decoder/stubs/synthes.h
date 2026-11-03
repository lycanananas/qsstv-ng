// decodertest stub: transmit is not used
#ifndef SYNTHES_H
#define SYNTHES_H
class synthesizer
{
public:
  void sendSamples(unsigned int, double) {}
  void sendSample(double) {}
};
extern synthesizer *synthesPtr;
#endif
