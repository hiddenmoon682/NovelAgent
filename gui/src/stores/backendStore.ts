/// NovelAgent GUI — 后端连接状态管理

import { create } from "zustand";
import type { BackendStatus } from "@/lib/types";

interface BackendState {
  status: BackendStatus;
  port: number;
  setStatus: (status: BackendStatus) => void;
}

export const useBackendStore = create<BackendState>((set) => ({
  status: "starting",
  port: 18899,
  setStatus: (status) => set({ status }),
}));
