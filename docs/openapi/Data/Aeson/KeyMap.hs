module Data.Aeson.KeyMap
  ( KeyMap
  , keys
  , lookup
  , empty
  , fromList
  , toList
  ) where

import Data.Aeson.Key (Key)
import Data.Map.Strict (Map)
import Prelude hiding (lookup)
import qualified Data.Map.Strict as M

type KeyMap v = Map Key v

keys :: KeyMap v -> [Key]
keys = M.keys

lookup :: Key -> KeyMap v -> Maybe v
lookup = M.lookup

empty :: KeyMap v
empty = M.empty

fromList :: [(Key, v)] -> KeyMap v
fromList = M.fromList

toList :: KeyMap v -> [(Key, v)]
toList = M.toList
