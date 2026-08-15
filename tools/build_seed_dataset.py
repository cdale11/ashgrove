#!/usr/bin/env python3
"""Phase 8 (B): build a deterministic canonical seed dataset.

Enumerates the game's fixed command surface (actions + slots + aliases) and
emits (utterance -> {action, parameters}) pairs as `data/dataset.jsonl`. This
seed is generated from the code itself, so it is guaranteed correct and
requires no cloud. The cloud teacher (`gen_dataset.py`) later expands these
canonical intents into natural-language paraphrases to broaden coverage.

Usage:
    tools/build_seed_dataset.py [--out data/dataset.jsonl]
"""
import argparse
import itertools
import json


# action: list of (slot-name, [values]) -- a None slot means no parameter.
# Each action yields one row per alias x slot-value combination.
SURFACE = {
    "look": [],
    "inventory": [],
    "status": [],
    "help": [],
    "sleep": [],
    "exit": [],
    "enter": [("building", ["farmhouse", "barn", "coop", "shop", "carpenter",
                            "blacksmith", "clinic", "museum", "cabin"])],
    "basement": [],
    "horror": [],
    "newgame": [],
    "hoe": [],
    "water": [],
    "harvest": [],
    "axe": [("tree", ["oak", "maple", "pine"])],
    "scythe": [],
    "fish": [],
    "tap": [("tree", ["oak", "maple", "pine"])],
    "shake": [("tree", ["oak", "maple", "pine"])],
    "plant": [("crop", ["parsnip", "potato", "cauliflower", "corn", "tomato",
                        "wheat", "strawberry", "melon", "pumpkin"])],
    "planttree": [("crop", ["oak", "maple", "birch", "cedar"])],
    "talk": [("npc", ["leah", "elliott", "maru", "penny", "haley", "sam",
                      "shane", "clint", "robin", "demetrius"])],
    "gift": [("item", ["parsnip", "flower", "strawberry", "wine"])],
    "hearts": [],
    "buy": [("item", ["parsnip seeds", "potato seeds", "bread", "bait"])],
    "sell": [("item", ["parsnip", "potato", "strawberry", "wine"])],
    "craft": [("recipe", ["bread", "scarecrow", "composter", "salad"])],
    "place": [("thing", ["sprinkler", "scarecrow", "composter", "barn", "coop"])],
    "repair": [("building", ["farmhouse", "barn", "coop"])],
    "upgrade": [("building", ["farmhouse"])],
    "plots": [],
    "train": [],
    "bus": [],
    "tv": [],
    "festival": [],
    "save": [],
    "load": [],
}

# Verb aliases that map to each action, in player-natural phrasings.
ALIASES = {
    "look": ["look", "l", "look around", "look around me", "examine my surroundings",
             "what do i see", "look here", "inspect"],
    "inventory": ["inventory", "inv", "what am i carrying", "show my items",
                  "what do i have", "bag", "my bag", "check inventory"],
    "status": ["status", "stats", "how am i doing", "my stats", "energy",
               "check status", "how do i feel"],
    "help": ["help", "?", "help me", "what can i do", "commands", "show help"],
    "sleep": ["sleep", "rest", "go to bed", "sleep for the night", "i want to sleep"],
    "exit": ["exit", "leave", "go outside", "leave this building", "walk out"],
    "enter": ["enter", "go inside", "walk into", "enter the", "go into"],
    "basement": ["basement", "open the basement", "go to the basement", "cellar"],
    "horror": ["horror", "check my sanity", "sanity", "my mental state"],
    "newgame": ["newgame", "new game", "restart", "start over", "reset game"],
    "hoe": ["hoe", "till", "plow", "till the soil", "hoe the ground"],
    "water": ["water", "water the crops", "water the soil", "sprinkle water"],
    "harvest": ["harvest", "pick", "harvest the crops", "pick the ripe crops",
                "reap", "collect crops"],
    "axe": ["axe", "chop", "cut down", "chop the", "cut the"],
    "scythe": ["scythe", "clear grass", "cut the weeds", "clear weeds", "mow"],
    "fish": ["fish", "cast my line", "go fishing", "fish here", "try to fish"],
    "tap": ["tap", "install a tapper", "tap the", "put a tapper on"],
    "shake": ["shake", "shake the tree", "shake the"],
    "plant": ["plant", "plant some", "sow", "plant the", "grow"],
    "planttree": ["planttree", "plant a tree", "plant a", "sow a tree"],
    "talk": ["talk", "talk to", "speak with", "chat with", "converse with"],
    "gift": ["gift", "give", "give a gift", "hand", "present"],
    "hearts": ["hearts", "friends", "friendship", "check friendship", "hearts level"],
    "buy": ["buy", "purchase", "buy some", "i want to buy", "get"],
    "sell": ["sell", "sell my", "i want to sell", "offload"],
    "craft": ["craft", "cook", "make", "bake", "craft a", "cook a", "make a"],
    "place": ["place", "build", "construct", "place a", "build a", "put down"],
    "repair": ["repair", "fix", "repair the", "fix the"],
    "upgrade": ["upgrade", "upgrade the", "expand", "upgrade my"],
    "plots": ["plots", "deeds", "land", "what plots can i buy", "buyable plots"],
    "train": ["train", "take the train", "catch the train", "go to the station"],
    "bus": ["bus", "take the bus", "catch the bus", "go to the plaza"],
    "tv": ["tv", "watch tv", "turn on the tv", "watch the news"],
    "festival": ["festival", "fest", "join the festival", "go to the festival"],
    "save": ["save", "save the game", "save game"],
    "load": ["load", "load the game", "load save"],
}

# Slot values phrased naturally after the alias. For slots with `the` prefix
# aliases we join "alias slotvalue"; otherwise "alias <slotvalue>".
def render(action, alias, slotname, slotval):
    text = alias
    if slotname is not None:
        # avoid double 'the' when alias already ends in 'the'
        if text.endswith(" the"):
            text += " " + slotval
        else:
            text += " " + slotval
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="data/dataset.jsonl")
    args = ap.parse_args()

    rows = []
    seen = set()
    for action, slots in SURFACE.items():
        aliases = ALIASES[action]
        if not slots:
            # one row per alias
            for a in aliases:
                text = a
                if text not in seen:
                    seen.add(text)
                    rows.append({"text": text, "intent": {"action": action,
                                                          "parameters": {}},
                                 "source": "seed"})
        else:
            slotname = slots[0][0]
            values = slots[0][1]
            for a, v in itertools.product(aliases, values):
                text = render(action, a, slotname, v)
                if text in seen:
                    continue
                seen.add(text)
                rows.append({"text": text,
                             "intent": {"action": action,
                                        "parameters": {slotname: v}},
                             "source": "seed"})

    with open(args.out, "w") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    print(f"wrote {len(rows)} seed rows to {args.out}")


if __name__ == "__main__":
    main()
