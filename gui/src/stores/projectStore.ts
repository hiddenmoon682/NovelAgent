/// NovelAgent GUI — 项目元数据状态管理

import { create } from "zustand";
import type { ProjectStatus, ChapterInfo, CharacterInfo } from "@/lib/types";
import * as api from "@/lib/api";

interface ProjectState {
  status: ProjectStatus | null;
  chapters: ChapterInfo[];
  characters: CharacterInfo[];
  loading: boolean;
  error: string | null;

  fetchStatus: () => Promise<void>;
  fetchChapters: () => Promise<void>;
  fetchCharacters: () => Promise<void>;
  fetchAll: () => Promise<void>;
}

export const useProjectStore = create<ProjectState>((set) => ({
  status: null,
  chapters: [],
  characters: [],
  loading: false,
  error: null,

  fetchStatus: async () => {
    try {
      const data = await api.getProjectStatus();
      set({ status: data, error: null });
    } catch (err) {
      set({ error: String(err) });
    }
  },

  fetchChapters: async () => {
    try {
      const data = await api.getChapters();
      set({ chapters: data, error: null });
    } catch (err) {
      set({ error: String(err) });
    }
  },

  fetchCharacters: async () => {
    try {
      const data = await api.getCharacters();
      set({ characters: data, error: null });
    } catch (err) {
      set({ error: String(err) });
    }
  },

  fetchAll: async () => {
    set({ loading: true, error: null });
    try {
      const [status, chapters, characters] = await Promise.all([
        api.getProjectStatus(),
        api.getChapters(),
        api.getCharacters(),
      ]);
      set({ status, chapters, characters, loading: false });
    } catch (err) {
      set({ error: String(err), loading: false });
    }
  },
}));
