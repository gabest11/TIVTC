/*
**                    TIVTC v1.0.14 for Avisynth 2.6 interface
**
**   TIVTC includes a field matching filter (TFM) and a decimation
**   filter (TDecimate) which can be used together to achieve an
**   IVTC or for other uses. TIVTC currently supports YV12 and
**   YUY2 colorspaces.
**
**   Copyright (C) 2004-2008 Kevin Stone, additional work (C) 2017 pinterf
**
**   This program is free software; you can redistribute it and/or modify
**   it under the terms of the GNU General Public License as published by
**   the Free Software Foundation; either version 2 of the License, or
**   (at your option) any later version.
**
**   This program is distributed in the hope that it will be useful,
**   but WITHOUT ANY WARRANTY; without even the implied warranty of
**   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**   GNU General Public License for more details.
**
**   You should have received a copy of the GNU General Public License
**   along with this program; if not, write to the Free Software
**   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <windows.h>
#include <stdio.h>
#include <malloc.h>
#include <math.h>
#include "internal.h"
#include "Font.h"
#include "Cycle.h"
#include "calcCRC.h"
#include "profUtil.h"
#include "Cache.h"
#include "memset_simd.h"
#define ISP 0x00000000 // p
#define ISC 0x00000001 // c
#define ISN 0x00000002 // n
#define ISB 0x00000003 // b
#define ISU 0x00000004 // u
#define ISDB 0x00000005 // l = (deinterlaced c bottom field)
#define ISDT 0x00000006 // h = (deinterlaced c top field)
#define MTC(n) n == 0 ? 'p' : n == 1 ? 'c' : n == 2 ? 'n' : n == 3 ? 'b' : n == 4 ? 'u' : \
			   n == 5 ? 'l' : n == 6 ? 'h' : 'x'
#define TOP_FIELD 0x00000008
#define D2VFILM 0x00000020
#define MAGIC_NUMBER (0xdeadfeed)
#define MAGIC_NUMBER_2 (0xdeadbeef)
#define DROP_FRAME 0x00000001 // ovr array - bit 1
#define KEEP_FRAME 0x00000002 // ovr array - 2
#define FILM 0x00000004	// ovr array - bit 3
#define VIDEO 0x00000008 // ovr array - bit 4
#define ISMATCH 0x00000070 // ovr array - bits 5-7
#define ISD2VFILM 0x00000080 // ovr array - bit 8
#define VERSION "v1.0.3.1"
#define cfps(n) n == 1 ? "119.880120" : n == 2 ? "59.940060" : n == 3 ? "39.960040" : \
				n == 4 ? "29.970030" : n == 5 ? "23.976024" : "unknown"

class TDecimate : public GenericVideoFilter
{
private:
  int nfrms, nfrmsN, nt, blockx, blocky, linearCount, maxndl;
  int yshiftS, xshiftS, xhalfS, yhalfS, mode, conCycleTP, opt;
  int cycleR, cycle, hybrid, vidDetect, conCycle, vfrDec, lastn;
  int lastFrame, lastCycle, lastGroup, lastType, retFrames;
  uint64_t MAX_DIFF, sceneThreshU, sceneDivU, diff_thresh, same_thresh;
  double rate, fps, mkvfps, mkvfps2, dupThresh, vidThresh, sceneThresh;
  bool debug, display, useTFMPP, batch, tcfv1, se, cve, ecf, fullInfo;
  bool noblend, m2PA, predenoise, chroma, exPP, ssd, usehints, useclip2;
  uint64_t *diff, *metricsArray, *metricsOutArray, *mode2_metrics;
  int *aLUT, *mode2_decA, *mode2_order, sdlim;
  unsigned int outputCrc;
  unsigned char *ovrArray;
  const char *ovr, *input, *output, *mkvOut, *tfmIn, *orgOut;
  int mode2_num, mode2_den, mode2_numCycles, mode2_cfs[10];
  Cycle prev, curr, next, nbuf;
  FILE *mkvOutF;
  PClip clip2;
  char buf[8192], outputFull[270];

  void init_mode_5(IScriptEnvironment* env);
  void rerunFromStart(int s, int np, IScriptEnvironment *env);
  void setBlack(PVideoFrame &dst, int np);
  void checkVideoMetrics(Cycle &c, double thresh);
  void checkVideoMatches(Cycle &p, Cycle &c);
  bool checkMatchDup(int mp, int mc);
  void copyFrame(PVideoFrame &dst, PVideoFrame &src, IScriptEnvironment *env, int np);
  void findDupStrings(Cycle &p, Cycle &c, Cycle &n, IScriptEnvironment *env);
  int getHint(PVideoFrame &src, int &d2vfilm);
  void restoreHint(PVideoFrame &dst, IScriptEnvironment *env);
  void blendFrames(PVideoFrame &src1, PVideoFrame &src2, PVideoFrame &dst,
    double amount1, double amount2, int np, IScriptEnvironment *env);
  void calcBlendRatios(double &amount1, double &amount2, int &frame1, int &frame2, int n,
    int bframe, int cycleI);
  void drawBoxYUY2(PVideoFrame &dst, int blockN, int xblocks);
  void drawBoxYV12(PVideoFrame &dst, int blockN, int xblocks);
  void drawBox(PVideoFrame &dst, int blockN, int xblocks, int np);
  int DrawYUY2(PVideoFrame &dst, int x1, int y1, const char *s, int start);
  int DrawYV12(PVideoFrame &dst, int x1, int y1, const char *s, int start);
  int Draw(PVideoFrame &dst, int x1, int y1, const char *s, int np, int start = 0);
  PVideoFrame GetFrameMode01(int n, IScriptEnvironment *env, int np);
  PVideoFrame GetFrameMode2(int n, IScriptEnvironment *env, int np);
  PVideoFrame GetFrameMode3(int n, IScriptEnvironment *env, int np);
  PVideoFrame GetFrameMode4(int n, IScriptEnvironment *env, int np);
  PVideoFrame GetFrameMode5(int n, IScriptEnvironment *env, int np);
  PVideoFrame GetFrameMode6(int n, IScriptEnvironment *env, int np);
  PVideoFrame GetFrameMode7(int n, IScriptEnvironment *env, int np);
  void getOvrFrame(int n, uint64_t &metricU, uint64_t &metricF);
  void getOvrCycle(Cycle &current, bool mode2);
  void displayOutput(IScriptEnvironment* env, PVideoFrame &dst, int n,
    int ret, bool film, double amount1, double amount2, int f1, int f2, int np);
  void formatMetrics(Cycle &current);
  void formatDups(Cycle &current);
  void formatDecs(Cycle &current);
  void formatMatches(Cycle &current);
  void formatMatches(Cycle &current, Cycle &previous);
  void debugOutput1(int n, bool film, int blend);
  void debugOutput2(int n, int ret, bool film, int f1, int f2, double amount1,
    double amount2);
  void addMetricCycle(Cycle &j);
  bool checkForObviousDecFrame(Cycle &p, Cycle &c, Cycle &n);
  void mostSimilarDecDecision(Cycle &p, Cycle &c, Cycle &n, IScriptEnvironment *env);
  int checkForD2VDecFrame(Cycle &p, Cycle &c, Cycle &n);
  bool checkForTwoDropLongestString(Cycle &p, Cycle &c, Cycle &n);
  int getNonDecMode2(int n, int start, int stop);
  double buildDecStrategy(IScriptEnvironment *env);
  void mode2MarkDecFrames(int cycleF);
  void removeMinN(int m, int n, int start, int stop);
  void removeMinN(int m, int n, uint64_t *metricsT, int *orderT, int &ovrC);
  int findDivisor(double decRatio, int min_den);
  int findNumerator(double decRatio, int divisor);
  double findCorrectionFactors(double decRatio, int num, int den, int rc[10], IScriptEnvironment *env);
  void sortMetrics(uint64_t *metrics, int *order, int length);
  //void SedgeSort(uint64_t *metrics, int *order, int length);
  //void pQuickerSort(uint64_t *metrics, int *order, int lower, int upper);
  void calcMetricCycle(Cycle &current, IScriptEnvironment *env, int np,
    bool scene, bool hnt);
  uint64_t calcMetric(PVideoFrame &prevt, PVideoFrame &currt, int np, int &blockNI,
    int &xblocksI, uint64_t &metricF, IScriptEnvironment *env, bool scene);

  uint64_t calcLumaDiffYUY2SSD(const unsigned char *prvp, const unsigned char *nxtp,
    int width, int height, int prv_pitch, int nxt_pitch, IScriptEnvironment *env);
  uint64_t calcLumaDiffYUY2SAD(const unsigned char *prvp, const unsigned char *nxtp,
    int width, int height, int prv_pitch, int nxt_pitch, IScriptEnvironment *env);


  void calcBlendRatios2(double &amount1, double &amount2, int &frame1,
    int &frame2, int tf, Cycle &p, Cycle &c, Cycle &n, int remove);
  bool similar_group(int f1, int f2, IScriptEnvironment *env);
  bool same_group(int f1, int f2, IScriptEnvironment *env);
  bool diff_group(int f1, int f2, IScriptEnvironment *env);
  int diff_f(int f1, int f2, IScriptEnvironment *env);
  int mode7_analysis(int n, IScriptEnvironment *env);

#ifdef ALLOW_MMX
  static void VerticalBlurMMX(const unsigned char *srcp, unsigned char *dstp, int src_pitch,
    int dst_pitch, int width, int height);
#endif
  static void VerticalBlurSSE2(const unsigned char *srcp, unsigned char *dstp, int src_pitch,
    int dst_pitch, int width, int height);

  template<bool use_sse2>
  static void HorizontalBlurMMXorSSE2_YV12(const unsigned char *srcp, unsigned char *dstp, int src_pitch,
    int dst_pitch, int width, int height);

  template<bool use_sse2>
  static void HorizontalBlurMMXorSSE2_YUY2_luma(const unsigned char *srcp, unsigned char *dstp, int src_pitch,
    int dst_pitch, int width, int height);

  static void VerticalBlur(PVideoFrame &src, PVideoFrame &dst, int np, bool bchroma,
    IScriptEnvironment *env, VideoInfo& vi_t, int opti);
  static void HorizontalBlur(PVideoFrame &src, PVideoFrame &dst, int np, bool bchroma,
    IScriptEnvironment *env, VideoInfo& vi_t, int opti);

  template<bool use_sse2>
  static void HorizontalBlurMMXorSSE2_YUY2(const unsigned char *srcp, unsigned char *dstp, int src_pitch,
    int dst_pitch, int width, int height);
  bool wasChosen(int i, int n);
  void calcMetricPreBuf(int n1, int n2, int pos, int np, bool scene, bool gethint,
    IScriptEnvironment *env);
public:
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env) override;
  TDecimate(PClip _child, int _mode, int _cycleR, int _cycle, double _rate,
    double _dupThresh, double _vidThresh, double _sceneThresh, int _hybrid,
    int _vidDetect, int _conCycle, int _conCycleTP, const char* _ovr,
    const char* _output, const char* _input, const char* _tfmIn, const char* _mkvOut,
    int _nt, int _blockx, int _blocky, bool _debug, bool _display, int _vfrDec,
    bool _batch, bool _tcfv1, bool _se, bool _chroma, bool _exPP, int _maxndl,
    bool _m2PA, bool _predenoise, bool _noblend, bool _ssd, int _usehints,
    PClip _clip2, int _sdlim, int _opt, const char* _orgOut, IScriptEnvironment* env);
  ~TDecimate();
  static void blurFrame(PVideoFrame &src, PVideoFrame &dst, int np, int iterations,
    bool bchroma, IScriptEnvironment *env, VideoInfo& vi_t, int opti);

  int __stdcall SetCacheHints(int cachehints, int frame_range) override {
    return cachehints == CACHE_GET_MTMODE ? MT_SERIALIZED : 0;
  }

};