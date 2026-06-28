#pragma once

// NovelAgent 的核心数据模型 — 聚合头。
//
// 结构层次:
//   Project
//   ├── Outline
//   │   ├── Volume[]
//   │   ├── PlotThread[]
//   │   └── Chapter[]
//   │       └── Scene[]
//   ├── Character[]
//   │   ├── Relationship[]
//   │   └── CharacterDevelopment[]
//   ├── Setting[]
//   ├── WorldRule[]
//   └── Style
//
// 子头文件位于 project/Models/ 目录：
//   ModelsFwd.h         — 统一前向声明
//   ModelDetail.h       — 内部辅助函数
//   Scene.h / Relationship.h / CharacterDevelopment.h
//   Character.h / Setting.h / WorldRule.h
//   PlotThread.h / Volume.h / Chapter.h
//   Style.h / Outline.h / Project.h

#include "project/Models/ModelDetail.h"
#include "project/Models/Scene.h"
#include "project/Models/Relationship.h"
#include "project/Models/CharacterDevelopment.h"
#include "project/Models/Character.h"
#include "project/Models/Setting.h"
#include "project/Models/WorldRule.h"
#include "project/Models/PlotThread.h"
#include "project/Models/Volume.h"
#include "project/Models/Chapter.h"
#include "project/Models/Style.h"
#include "project/Models/Outline.h"
#include "project/Models/Project.h"
