import type { MasterFrame } from "./panels/MasterPanel";

/** Live maximizer series, or undefined when the host has no analysis frame. */
export const maximizerPlotValues = (getFrame: (() => MasterFrame | undefined) | undefined) =>
  getFrame
    ? {
        shortTermLufs: () => getFrame()?.lufs?.shortTerm ?? -70,
        gainReductionDb: () => {
          const gr = getFrame()?.meters?.gr ?? 0;
          return gr === 0 ? 0 : -gr;
        },
      }
    : undefined;
