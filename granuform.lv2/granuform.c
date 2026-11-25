/* Granuform — greffon LV2 granulaire (mono/stéréo, rotation Doppler,
 * vibrato, glissando, chaos). Licence MIT. */
#include <lv2/core/lv2.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GRANUFORM_URI "https://mace.vibe/granuform"
#define MAX_GRAINS 128
#define BUF_SECONDS 5.0f

typedef enum {
  P_IN_L = 0, P_IN_R, P_OUT_L, P_OUT_R,
  P_DENSITY, P_SIZE, P_JITTER,
  P_PITCH, P_MODE, P_ROT_SPEED, P_DOPPLER,
  P_VIB_RATE, P_VIB_CENTS,
  P_GLI_SEMIS, P_GLI_PERIOD,
  P_CHAOS,
  P_MIX, P_GAIN,
  NUM_PORTS
} PortIdx;

typedef struct {
  int     active;
  double  onset;   /* horloge échantillon du début du grain */
  double  dur;     /* durée en échantillons */
  uint32_t src;    /* position de lecture dans le tampon circulaire */
  double  rate;    /* rapport de lecture de base */
  float   pan, gl, gr; /* panoramique et gains précalculés */
  float   amp;
  int     side;    /* 0 = lit L, 1 = lit R, 2 = lit le mixdown */
} Grain;

typedef struct {
  float*   ports[NUM_PORTS];
  double   sr;
  uint64_t clock;      /* horloge échantillon totale */
  float*   bufL;
  float*   bufR;
  uint32_t bufLen;
  uint32_t bufPos;
  uint64_t filled;
  Grain    grains[MAX_GRAINS];
  double   nextGrain;  /* horloge du prochain grain */
  uint32_t rnd;
} Granuform;

static uint32_t xr(Granuform* g) {
  g->rnd ^= g->rnd << 13; g->rnd ^= g->rnd >> 17; g->rnd ^= g->rnd << 5;
  return g->rnd;
}
static double xr01(Granuform* g) { return xr(g) / 4294967296.0; }
static double pclamp(const float* p, double lo, double hi) {
  double v = *p; return v < lo ? lo : (v > hi ? hi : v);
}

static void spawn(Granuform* g) {
  const double sr      = g->sr;
  const double density = pclamp(g->ports[P_DENSITY], 1.0, 64.0);
  const double sizeS   = pclamp(g->ports[P_SIZE], 0.01, 1.0);
  const double jit     = pclamp(g->ports[P_JITTER], 0.0, 1.0);
  const double pitch   = pclamp(g->ports[P_PITCH], -24.0, 24.0);
  const int    stereo  = *g->ports[P_MODE] > 0.5f;
  const double rot     = pclamp(g->ports[P_ROT_SPEED], 0.0, 5.0);
  const double dop     = pclamp(g->ports[P_DOPPLER], 0.0, 0.2);
  const double chaos   = *g->ports[P_CHAOS] > 0.5f;

  const uint32_t dur  = (uint32_t)(sizeS * sr);
  uint32_t avail = g->filled < g->bufLen ? (uint32_t)g->filled : g->bufLen;
  if (avail < 256) return;
  const uint32_t region = (uint32_t)fmin((double)avail, fmax(256.0, jit * (double)avail));

  double rate = pow(2.0, pitch / 12.0);
  if (chaos) rate *= pow(2.0, floor(xr01(g) * 12.0) / 12.0);

  float amp = (float)fmin(0.55, 1.4 / sqrt(density * sizeS));
  amp *= 0.8f + 0.4f * (float)xr01(g);

  const double tSec = g->nextGrain / sr;
  double w;
  if (rot > 0.001) { w = 0.62 + 0.33 * sin(2.0 * M_PI * rot * tSec); }
  else             { w = 0.86; }

  int slots[2] = { -1, -1 };
  int want = stereo ? 2 : 1;
  for (int k = 0, f = 0; k < MAX_GRAINS && f < want; k++)
    if (!g->grains[k].active) slots[f++] = k;
  if (slots[0] < 0) return;

  for (int s = 0; s < want; s++) {
    Grain* gr = &g->grains[slots[s]];
    uint32_t src = (g->bufPos + g->bufLen - region + (xr(g) % region)) % g->bufLen;
    gr->active = 1;
    gr->onset  = g->nextGrain;
    gr->dur    = (double)dur;
    gr->src    = src;
    gr->rate   = rate;
    gr->amp    = amp * (stereo ? 0.72f : 1.0f);
    if (stereo) {
      gr->side = s;                    /* 0 gauche, 1 droite */
      gr->pan  = s == 0 ? (float)-w : (float)w;
    } else {
      gr->side = 2;                     /* mixdown spatialisé */
      gr->pan  = rot > 0.001
        ? (float)sin(2.0 * M_PI * rot * tSec)
        : (float)(xr01(g) * 2.0 - 1.0);
      if (dop > 0.0) gr->rate *= 1.0 + dop * cos(2.0 * M_PI * rot * tSec);
    }
    const float th = (gr->pan + 1.0f) * (float)(M_PI * 0.25);
    gr->gl = cosf(th); gr->gr = sinf(th);
  }
}

static void run(LV2_Handle h, uint32_t n) {
  Granuform* g = (Granuform*)h;
  float* inL = g->ports[P_IN_L];  float* inR = g->ports[P_IN_R];
  float* oL  = g->ports[P_OUT_L]; float* oR  = g->ports[P_OUT_R];
  const double sr    = g->sr;
  const double dens  = pclamp(g->ports[P_DENSITY], 1.0, 64.0);
  const double vrate = pclamp(g->ports[P_VIB_RATE], 0.0, 12.0);
  const double vcents= pclamp(g->ports[P_VIB_CENTS], 0.0, 100.0);
  const double gsemi = pclamp(g->ports[P_GLI_SEMIS], -24.0, 24.0);
  const double gper  = pclamp(g->ports[P_GLI_PERIOD], 0.2, 32.0);
  const double rot   = pclamp(g->ports[P_ROT_SPEED], 0.0, 5.0);
  const double dop   = pclamp(g->ports[P_DOPPLER], 0.0, 0.2);
  const double mix   = pclamp(g->ports[P_MIX], 0.0, 1.0);
  const double gain  = pclamp(g->ports[P_GAIN], 0.0, 4.0);

  for (uint32_t i = 0; i < n; i++) {
    g->bufL[g->bufPos] = inL[i];
    g->bufR[g->bufPos] = inR[i];
    g->bufPos = (g->bufPos + 1) % g->bufLen;
    if (g->filled < g->bufLen) g->filled++;

    const uint64_t now = g->clock + i;
    while (now >= g->nextGrain) {
      spawn(g);
      g->nextGrain += sr / dens * (0.8 + 0.4 * xr01(g));
    }

    float l = 0.0f, r = 0.0f;
    for (int k = 0; k < MAX_GRAINS; k++) {
      Grain* gr = &g->grains[k];
      if (!gr->active) continue;
      const double tau = (double)now - gr->onset;
      if (tau < 0.0) continue;
      if (tau >= gr->dur) { gr->active = 0; continue; }
      const double tauS = tau / sr;
      const float ph = (float)(tau / gr->dur);
      float env = sinf((float)M_PI * ph); env *= env;

      double semis = 0.0, doppler = 1.0;
      if (gsemi != 0.0) {
        double frac = tauS / gper; frac -= floor(frac);
        semis += gsemi * (2.0 * frac - 1.0);
      }
      if (vcents != 0.0) semis += vcents * 0.01 * sin(2.0 * M_PI * vrate * tauS);
      if (gr->side == 2 && dop != 0.0) doppler = 1.0 + dop * cos(2.0 * M_PI * rot * tauS);

      const uint32_t rd = (gr->src + (uint32_t)(tau * gr->rate * pow(2.0, semis / 12.0) * doppler)) % g->bufLen;
      const float s = gr->side == 0 ? g->bufL[rd]
                    : gr->side == 1 ? g->bufR[rd]
                    : 0.5f * (g->bufL[rd] + g->bufR[rd]);
      const float e = env * gr->amp;
      l += s * e * gr->gl;
      r += s * e * gr->gr;
    }
    oL[i] = l * (float)(mix * gain) + inL[i] * (float)((1.0 - mix) * gain);
    oR[i] = r * (float)(mix * gain) + inR[i] * (float)((1.0 - mix) * gain);
  }
  g->clock += n;
}

static LV2_Handle instantiate(const LV2_Descriptor* d, double rate,
                              const char* bundle, const LV2_Feature* const* f) {
  (void)d; (void)bundle; (void)f;
  Granuform* g = (Granuform*)calloc(1, sizeof(Granuform));
  if (!g) return NULL;
  g->sr = rate;
  g->bufLen = (uint32_t)(rate * BUF_SECONDS);
  g->bufL = (float*)calloc(g->bufLen, sizeof(float));
  g->bufR = (float*)calloc(g->bufLen, sizeof(float));
  g->rnd = 0x9E3779B9u;
  if (!g->bufL || !g->bufR) { free(g->bufL); free(g->bufR); free(g); return NULL; }
  return (LV2_Handle)g;
}
static void connect_port(LV2_Handle h, uint32_t port, void* data) {
  ((Granuform*)h)->ports[port] = (float*)data;
}
static void activate(LV2_Handle h) {
  Granuform* g = (Granuform*)h;
  g->clock = 0; g->filled = 0; g->bufPos = 0; g->nextGrain = 0;
  memset(g->bufL, 0, g->bufLen * sizeof(float));
  memset(g->bufR, 0, g->bufLen * sizeof(float));
  for (int k = 0; k < MAX_GRAINS; k++) g->grains[k].active = 0;
}
static void cleanup(LV2_Handle h) {
  Granuform* g = (Granuform*)h;
  free(g->bufL); free(g->bufR); free(g);
}
static const void* extension_data(const char* uri) { (void)uri; return NULL; }

static const LV2_Descriptor descr = {
  GRANUFORM_URI, instantiate, connect_port, activate,
  run, NULL, cleanup, extension_data
};
LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t idx) {
  return idx == 0 ? &descr : NULL;
}
