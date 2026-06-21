module Data.HashMap.Strict
  ( HashMap
  , empty
  , null
  , keys
  , elems
  , toList
  , fromList
  ) where

import Data.Map.Strict (Map)
import Prelude hiding (null)
import qualified Data.Map.Strict as M

type HashMap k v = Map k v

empty :: HashMap k v
empty = M.empty

null :: HashMap k v -> Bool
null = M.null

keys :: HashMap k v -> [k]
keys = M.keys

elems :: HashMap k v -> [v]
elems = M.elems

toList :: HashMap k v -> [(k, v)]
toList = M.toList

fromList :: Ord k => [(k, v)] -> HashMap k v
fromList = M.fromList
