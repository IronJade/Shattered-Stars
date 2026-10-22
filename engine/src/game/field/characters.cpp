#include "game/field/characters.h"

#include <cstring>
#include <utility>

#include "game/boot/rng.h"
#include "game/field/field.h"
#include "runtime/runtime.h"

// Generated from the decompilation of 0x8007a37c-0x8007b0c0 (straight lists of stores;
// repeated stores are in the original).
namespace ss::game::field {
namespace {

void put16(u8* p, int offset, u32 v) {
  const u16 x = static_cast<u16>(v);
  std::memcpy(p + offset, &x, 2);
}
void put32(u8* p, int offset, u32 v) { std::memcpy(p + offset, &v, 4); }

// 0x8007a37c: Claude's starting data.
void init_claude(u8* slot, u8* record) {
  put32(slot, 0x14, 130);
  put32(slot, 0x1C, 130);
  put32(slot, 0x18, 130);
  put16(slot, 0x30, 5);
  put16(slot, 0x4E, 16);
  put16(slot, 0x54, 9);
  put16(slot, 0x28, 1);
  put16(slot, 0x20, 20);
  put16(slot, 0x24, 20);
  put16(slot, 0x22, 20);
  put16(slot, 0x2A, 10);
  put16(slot, 0x36, 10);
  put16(slot, 0x3C, 10);
  put16(slot, 0x46, 10);
  slot[0x2] = 0;
  put16(slot, 0x48, 20);
  put32(slot, 0x10, 0);
  put16(record, 0x6, 15);
  put16(record, 0x0, 132);
  put16(record, 0xC, 0);
  put16(record, 0xE, 0);
  put16(record, 0x10, 0);
  put16(record, 0x12, 0);
  put16(record, 0x14, 0);
  put16(record, 0x16, 0);
  put16(record, 0x18, 0);
  put16(record, 0xC, 0);
  put16(record, 0xE, 0);
  put16(record, 0x10, 0);
  put16(record, 0x12, 0);
  put16(record, 0x14, 0);
  put16(record, 0x16, 0);
  put16(record, 0x18, 0);
  record[0x3E] = 1;
  record[0xCC] = 3;
  record[0xCD] = 0;
  record[0x72] = 1;
  record[0x77] = 1;
  slot[0x3] = 1;
  roll_talents(record, field_data(0x8007B73C));
  std::memcpy(record + 0x24, field_data(0x8007B6DC), 7);  // "Claude"
}

// 0x8007a49c: Rena's starting data.
void init_rena(u8* slot, u8* record) {
  put32(slot, 0x14, 100);
  put32(slot, 0x1C, 100);
  put32(slot, 0x18, 100);
  put16(slot, 0x20, 40);
  put16(slot, 0x24, 40);
  put16(slot, 0x22, 40);
  put16(slot, 0x2A, 5);
  put16(slot, 0x30, 2);
  put16(slot, 0x36, 8);
  put16(slot, 0x46, 15);
  put16(slot, 0x4E, 15);
  put16(slot, 0x54, 6);
  put16(slot, 0x48, 30);
  put16(slot, 0x28, 1);
  put16(slot, 0x3C, 10);
  slot[0x2] = 0;
  put32(slot, 0x10, 0);
  put16(record, 0x0, 130);
  put16(record, 0xC, 539);
  put16(record, 0x6, 10);
  put16(record, 0xE, 681);
  put16(record, 0x10, 0);
  put16(record, 0x12, 0);
  put16(record, 0x14, 0);
  put16(record, 0x16, 0);
  put16(record, 0x18, 0);
  record[0x3C] = 1;
  record[0x6C] = 1;
  record[0x6D] = 4;
  record[0x6E] = 1;
  slot[0x3] = 9;
  roll_talents(record, field_data(0x8007B764));
  record[0x3C] = 1;  // a one-pass loop in the original
  std::memcpy(record + 0x24, field_data(0x8007B6E4), 5);  // "Rena"
}

// 0x8007a5bc: Celine's starting data.
void init_celine(u8* slot, u8* record) {
  put32(slot, 0x14, 400);
  put32(slot, 0x1C, 400);
  put32(slot, 0x18, 400);
  put16(slot, 0x20, 100);
  put16(slot, 0x24, 100);
  put16(slot, 0x22, 100);
  put16(slot, 0x36, 5);
  put16(slot, 0x3C, 12);
  put16(slot, 0x46, 20);
  put16(slot, 0x54, 6);
  put16(slot, 0x48, 40);
  put32(slot, 0x10, 735);
  put16(slot, 0x2A, 15);
  put16(slot, 0x30, 8);
  put16(slot, 0x4E, 15);
  slot[0x2] = 0;
  put16(slot, 0x28, 8);
  put16(record, 0x6, 14);
  put16(record, 0x0, 154);
  put16(record, 0xC, 568);
  put16(record, 0xE, 681);
  put16(record, 0x14, 741);
  put16(record, 0x16, 127);
  put16(record, 0x18, 118);
  put16(record, 0x10, 0);
  put16(record, 0x12, 0);
  record[0x3C] = 1;
  record[0x3F] = 1;
  record[0x42] = 1;
  record[0x63] = 1;
  record[0x61] = 1;
  record[0x64] = 5;
  record[0x72] = 2;
  record[0x73] = 2;
  roll_talents(record, field_data(0x8007B854));
  slot[0x3] = 11;
  std::memcpy(record + 0x24, field_data(0x8007B6EC), 7);  // "Celine"
}

// 0x8007a700: Bowman's starting data.
void init_bowman(u8* slot, u8* record) {
  put32(slot, 0x14, 1500);
  put32(slot, 0x1C, 1500);
  put32(slot, 0x18, 1500);
  put16(slot, 0x20, 170);
  put16(slot, 0x24, 170);
  put16(slot, 0x22, 170);
  put16(slot, 0x2A, 90);
  put16(slot, 0x30, 50);
  put16(slot, 0x36, 5);
  put16(slot, 0x3C, 62);
  put16(slot, 0x4E, 16);
  put16(slot, 0x54, 8);
  put16(slot, 0x48, 36);
  put16(slot, 0x28, 25);
  put32(slot, 0x10, 31183);
  put16(slot, 0x46, 35);
  slot[0x2] = 0;
  put16(record, 0xC, 541);
  put16(record, 0xE, 684);
  put16(record, 0x14, 732);
  put16(record, 0x0, 118);
  put16(record, 0x6, 35);
  record[0x3C] = 1;
  record[0x3D] = 1;
  record[0x3F] = 1;
  record[0x41] = 1;
  record[0xCC] = 1;
  put16(record, 0x10, 0);
  put16(record, 0x12, 0);
  put16(record, 0x16, 0);
  put16(record, 0x18, 0);
  record[0xCD] = 2;
  put16(record, 0x1A, 100);
  record[0x6A] = 4;
  record[0x62] = 5;
  record[0x6B] = 2;
  slot[0x3] = 2;
  roll_talents(record, field_data(0x8007B7DC));
  std::memcpy(record + 0x24, field_data(0x8007B6F4), 7);  // "Bowman"
}

// 0x8007a840: Dias's starting data.
void init_dias(u8* slot, u8* record) {
  put32(slot, 0x14, 2500);
  put32(slot, 0x1C, 2500);
  put32(slot, 0x18, 2500);
  put16(slot, 0x20, 250);
  put16(slot, 0x24, 250);
  put16(slot, 0x22, 250);
  put16(slot, 0x2A, 175);
  put16(slot, 0x30, 70);
  put16(slot, 0x36, 20);
  put16(slot, 0x3C, 120);
  put16(slot, 0x46, 64);
  put16(slot, 0x4E, 16);
  put16(slot, 0x54, 8);
  put16(slot, 0x48, 50);
  put16(slot, 0x28, 35);
  slot[0x2] = 0;
  put32(slot, 0x10, 96982);
  put16(record, 0x1A, 200);
  put16(record, 0xC, 492);
  put16(record, 0xE, 684);
  put16(record, 0x10, 715);
  put16(record, 0x12, 663);
  put16(record, 0x14, 739);
  put16(record, 0x16, 156);
  put16(record, 0x6, 45);
  put16(record, 0x0, 96);
  record[0x40] = 1;
  record[0x43] = 1;
  record[0x3C] = 1;
  record[0x42] = 1;
  put16(record, 0x18, 0);
  record[0xCC] = 5;
  record[0xCD] = 8;
  slot[0x3] = 4;
  roll_talents(record, field_data(0x8007B8F4));
  std::memcpy(record + 0x24, field_data(0x8007B6FC), 5);  // "Dias"
}

// 0x8007a97c: Precis's starting data.
void init_precis(u8* slot, u8* record) {
  put32(slot, 0x14, 950);
  put32(slot, 0x1C, 950);
  put32(slot, 0x18, 950);
  put16(slot, 0x2A, 42);
  put16(slot, 0x30, 31);
  put16(slot, 0x36, 5);
  put16(slot, 0x3C, 40);
  put16(slot, 0x46, 50);
  put16(slot, 0x54, 7);
  put16(slot, 0x48, 32);
  put32(slot, 0x10, 5779);
  put16(slot, 0x20, 100);
  put16(slot, 0x24, 100);
  put16(slot, 0x22, 100);
  put16(slot, 0x4E, 15);
  slot[0x2] = 0;
  put16(slot, 0x28, 15);
  put16(record, 0x6, 18);
  put16(record, 0x0, 153);
  put16(record, 0xC, 594);
  put16(record, 0xE, 684);
  put16(record, 0x10, 710);
  put16(record, 0x14, 730);
  put16(record, 0x1A, 100);
  put16(record, 0x16, 85);
  record[0xCC] = 7;
  record[0xCD] = 8;
  put16(record, 0x12, 0);
  put16(record, 0x18, 0);
  record[0x42] = 1;
  record[0x43] = 1;
  record[0x7B] = 1;
  record[0x7C] = 1;
  record[0x77] = 3;
  slot[0x3] = 5;
  roll_talents(record, field_data(0x8007B7B4));
  std::memcpy(record + 0x24, field_data(0x8007B704), 7);  // "Precis"
}

// 0x8007aabc: Ashton's starting data.
void init_ashton(u8* slot, u8* record) {
  put32(slot, 0x14, 1000);
  put32(slot, 0x1C, 1000);
  put32(slot, 0x18, 1000);
  put16(slot, 0x20, 120);
  put16(slot, 0x24, 120);
  put16(slot, 0x22, 120);
  put16(slot, 0x2A, 83);
  put16(slot, 0x36, 5);
  put16(slot, 0x3C, 50);
  put16(slot, 0x46, 25);
  put16(slot, 0x4E, 16);
  put16(slot, 0x54, 8);
  put16(slot, 0x28, 17);
  put32(slot, 0x10, 8677);
  put16(slot, 0x30, 32);
  slot[0x2] = 0;
  put16(slot, 0x48, 32);
  put16(record, 0xC, 523);
  put16(record, 0xE, 682);
  put16(record, 0x12, 660);
  put16(record, 0x14, 730);
  put16(record, 0x16, 85);
  put16(record, 0x18, 68);
  put16(record, 0x6, 18);
  put16(record, 0x0, 18);
  put16(record, 0x1A, 100);
  record[0x3E] = 1;
  record[0x3D] = 1;
  record[0x43] = 1;
  record[0xCC] = 3;
  put16(record, 0x10, 0);
  record[0xCD] = 2;
  slot[0x3] = 6;
  roll_talents(record, field_data(0x8007B78C));
  std::memcpy(record + 0x24, field_data(0x8007B70C), 7);  // "Ashton"
}

// 0x8007abf4: Leon's starting data.
void init_leon(u8* slot, u8* record) {
  put32(slot, 0x14, 1600);
  put32(slot, 0x1C, 1600);
  put32(slot, 0x18, 1600);
  put16(slot, 0x20, 300);
  put16(slot, 0x24, 300);
  put16(slot, 0x22, 300);
  put16(slot, 0x2A, 40);
  put16(slot, 0x36, 5);
  put16(slot, 0x3C, 35);
  put16(slot, 0x46, 75);
  put16(slot, 0x4E, 15);
  put16(slot, 0x54, 6);
  put16(slot, 0x48, 10);
  put32(slot, 0x10, 56803);
  put16(slot, 0x30, 30);
  slot[0x2] = 0;
  put16(slot, 0x28, 30);
  put16(record, 0x6, 23);
  put16(record, 0x0, 120);
  put16(record, 0xC, 624);
  put16(record, 0xE, 685);
  put16(record, 0x14, 732);
  put16(record, 0x10, 0);
  put16(record, 0x12, 0);
  put16(record, 0x16, 0);
  put16(record, 0x18, 0);
  record[0x3C] = 1;
  record[0x3D] = 1;
  record[0x3E] = 1;
  record[0x40] = 1;
  record[0x41] = 1;
  record[0x44] = 1;
  record[0x49] = 1;
  record[0x4A] = 1;
  record[0x72] = 4;
  record[0x73] = 1;
  record[0x61] = 3;
  slot[0x3] = 10;
  roll_talents(record, field_data(0x8007B87C));
  std::memcpy(record + 0x24, field_data(0x8007B714), 5);  // "Leon"
}

// 0x8007ad28: Opera's starting data.
void init_opera(u8* slot, u8* record) {
  put32(slot, 0x14, 1200);
  put32(slot, 0x1C, 1200);
  put32(slot, 0x18, 1200);
  put16(slot, 0x20, 140);
  put16(slot, 0x24, 140);
  put16(slot, 0x22, 140);
  put16(slot, 0x2A, 60);
  put16(slot, 0x30, 31);
  put16(slot, 0x36, 30);
  put16(slot, 0x3C, 52);
  put16(slot, 0x46, 44);
  put16(slot, 0x4E, 16);
  put16(slot, 0x54, 7);
  put16(slot, 0x48, 40);
  put32(slot, 0x10, 16945);
  slot[0x2] = 0;
  put16(slot, 0x28, 21);
  put16(record, 0xC, 581);
  put16(record, 0xE, 682);
  put16(record, 0x12, 658);
  put16(record, 0x14, 737);
  put16(record, 0x16, 61);
  put16(record, 0x6, 21);
  put16(record, 0x0, 142);
  put16(record, 0x10, 0);
  put16(record, 0x18, 0);
  record[0x40] = 1;
  record[0x3E] = 1;
  record[0xCC] = 3;
  record[0xCD] = 5;
  record[0x7C] = 3;
  record[0x63] = 1;
  record[0x71] = 1;
  record[0x77] = 1;
  slot[0x3] = 3;
  roll_talents(record, field_data(0x8007B804));
  std::memcpy(record + 0x24, field_data(0x8007B71C), 6);  // "Opera"
}

// 0x8007ae54: Ernest's starting data.
void init_ernest(u8* slot, u8* record) {
  put32(slot, 0x14, 1600);
  put32(slot, 0x1C, 1600);
  put32(slot, 0x18, 1600);
  put16(slot, 0x20, 160);
  put16(slot, 0x24, 160);
  put16(slot, 0x22, 160);
  put16(slot, 0x2A, 100);
  put16(slot, 0x30, 37);
  put16(slot, 0x36, 50);
  put16(slot, 0x3C, 60);
  put16(slot, 0x46, 60);
  put16(slot, 0x4E, 16);
  put16(slot, 0x54, 9);
  put16(slot, 0x48, 45);
  put16(slot, 0x28, 25);
  put32(slot, 0x10, 31183);
  slot[0x2] = 0;
  put16(record, 0xC, 611);
  put16(record, 0xE, 684);
  put16(record, 0x14, 730);
  put16(record, 0x6, 23);
  put16(record, 0x0, 121);
  record[0x3C] = 1;
  record[0x42] = 1;
  record[0xCC] = 1;
  record[0x6F] = 4;
  put16(record, 0x10, 0);
  put16(record, 0x12, 0);
  put16(record, 0x16, 0);
  put16(record, 0x18, 0);
  record[0xCD] = 7;
  record[0x62] = 5;
  record[0x68] = 5;
  slot[0x3] = 7;
  roll_talents(record, field_data(0x8007B82C));
  std::memcpy(record + 0x24, field_data(0x8007B724), 7);  // "Ernest"
}

// 0x8007af84: Noel's starting data.
void init_noel(u8* slot, u8* record) {
  put32(slot, 0x14, 2700);
  put32(slot, 0x1C, 2700);
  put32(slot, 0x18, 2700);
  put16(slot, 0x20, 340);
  put16(slot, 0x24, 340);
  put16(slot, 0x22, 340);
  put16(slot, 0x2A, 61);
  put16(slot, 0x30, 55);
  put16(slot, 0x36, 5);
  put16(slot, 0x3C, 50);
  put16(slot, 0x46, 80);
  put16(slot, 0x4E, 15);
  put16(slot, 0x54, 7);
  put16(slot, 0x48, 28);
  put16(slot, 0x28, 40);
  slot[0x2] = 0;
  put32(slot, 0x10, 151862);
  put16(record, 0x1A, 290);
  put16(record, 0x6, 30);
  put16(record, 0x0, 87);
  put16(record, 0xC, 543);
  put16(record, 0xE, 691);
  put16(record, 0x14, 732);
  put16(record, 0x16, 70);
  put16(record, 0x10, 0);
  put16(record, 0x12, 0);
  put16(record, 0x18, 0);
  record[0x3C] = 1;
  record[0x3D] = 1;
  record[0x3E] = 1;
  record[0x3F] = 1;
  record[0x41] = 1;
  record[0x42] = 1;
  record[0x44] = 1;
  record[0x46] = 1;
  record[0x47] = 1;
  record[0x48] = 1;
  slot[0x3] = 12;
  roll_talents(record, field_data(0x8007B8A4));
  std::memcpy(record + 0x24, field_data(0x8007B72C), 5);  // "Noel"
}

// 0x8007b0c0: Chisato's starting data.
void init_chisato(u8* slot, u8* record) {
  put32(slot, 0x14, 3000);
  put32(slot, 0x1C, 3000);
  put32(slot, 0x18, 3000);
  put16(slot, 0x20, 310);
  put16(slot, 0x24, 310);
  put16(slot, 0x22, 310);
  put16(slot, 0x2A, 164);
  put16(slot, 0x30, 60);
  put16(slot, 0x36, 50);
  put16(slot, 0x3C, 150);
  put16(slot, 0x46, 100);
  put16(slot, 0x4E, 16);
  put16(slot, 0x54, 7);
  put16(slot, 0x48, 42);
  put16(slot, 0x28, 40);
  slot[0x2] = 0;
  put32(slot, 0x10, 151862);
  put16(record, 0x1A, 290);
  put16(record, 0x6, 32);
  put16(record, 0x0, 119);
  put16(record, 0xC, 641);
  put16(record, 0xE, 691);
  put16(record, 0x12, 666);
  put16(record, 0x14, 739);
  put16(record, 0x16, 133);
  record[0x7A] = 4;
  put16(record, 0x10, 0);
  put16(record, 0x18, 0);
  record[0x3C] = 1;
  record[0x3E] = 1;
  record[0x43] = 1;
  record[0xCC] = 1;
  record[0xCD] = 3;
  slot[0x3] = 8;
  roll_talents(record, field_data(0x8007B8CC));
  std::memcpy(record + 0x24, field_data(0x8007B734), 8);  // "Chisato"
}

}  // namespace

void roll_talents(u8* record, const u8* table) {
  auto chance = [&](int order) {
    u16 c;
    std::memcpy(&c, table + order * 2 + 2, 2);
    return c;
  };
  put32(record, 0x20, 0);
  int order[10];
  for (int i = 0; i < 10; ++i) order[i] = i * 2;
  for (int i = 0; i < 100; ++i) {
    const u32 a = rng_range(10), b = rng_range(10);
    std::swap(order[a], order[b]);
  }
  for (int i = 1; i < 10; ++i)
    if (chance(order[i]) == 100) std::swap(order[0], order[i]);
  int picked = 0;
  for (int i = 0; i < 10 && picked < 4; ++i) {
    const u16 c = chance(order[i]);
    if (rng_range(100) < c) {
      ++picked;
      u16 bit;
      std::memcpy(&bit, table + order[i] * 2, 2);
      u32 talents;
      std::memcpy(&talents, record + 0x20, 4);
      put32(record, 0x20, talents | bit);
    }
  }
}

void party_slot_init(u8* slot, u8* record, int character) {
  std::memset(slot, 0, 0x60);
  std::memset(record, 0, 0xD0);
  character_init_data(slot, record, character);
  put16(slot, 0, static_cast<u32>(character & 0xFF));
}

void character_init_data(u8* slot, u8* record, int character) {
  // The talent tables and names come from the field program's image; without a disc
  // (unit tests) the member keeps the cleared slot.
  if (field_data(0x8007B6FC) == nullptr) {
    runtime::Runtime::get().log_once("character_init_data", "character starting data: no field program image");
    return;
  }
  switch (character) {
    case 1: init_claude(slot, record); break;
    case 2: init_rena(slot, record); break;
    case 3: init_celine(slot, record); break;
    case 4: init_bowman(slot, record); break;
    case 5: init_dias(slot, record); break;
    case 6: init_precis(slot, record); break;
    case 7: init_ashton(slot, record); break;
    case 8: init_leon(slot, record); break;
    case 9: init_opera(slot, record); break;
    case 10: init_ernest(slot, record); break;
    case 11: init_noel(slot, record); break;
    case 12: init_chisato(slot, record); break;
    default: break;
  }
}

}  // namespace ss::game::field
