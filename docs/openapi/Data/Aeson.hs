{-# LANGUAGE FlexibleInstances #-}
{-# LANGUAGE TypeSynonymInstances #-}

module Data.Aeson
  ( FromJSON(..)
  , ToJSON(..)
  , Value(..)
  , Object
  , encode
  , decode
  , object
  , withObject
  , (.:?)
  , (.!=)
  , (.=)
  ) where

import Data.Aeson.Key (Key)
import Data.Aeson.KeyMap (KeyMap)
import Data.Aeson.Types (Parser)
import Data.ByteString.Lazy (ByteString)
import Data.Int (Int64)
import Data.Map.Strict (Map)
import Data.Text (Text)
import Numeric.Natural (Natural)
import Prelude hiding (lookup)
import qualified Data.Aeson.KeyMap as KM
import qualified Data.ByteString.Lazy.Char8 as LBS
import qualified Data.Map.Strict as M

type Object = KeyMap Value

data Value
  = Object Object
  | Array [Value]
  | String Text
  | Number Double
  | Bool Bool
  | Null
  deriving (Show, Eq)

class FromJSON a where
  parseJSON :: Value -> Parser a
  parseJSON _ = Left "JSON parsing is not available in the diagnostic shim"

class ToJSON a where
  toJSON :: a -> Value
  toJSON _ = Null

encode :: ToJSON a => a -> ByteString
encode _ = LBS.pack "null"

decode :: FromJSON a => ByteString -> Maybe a
decode _ = Nothing

object :: [(Key, Value)] -> Value
object = Object . KM.fromList

withObject :: String -> (Object -> Parser a) -> Value -> Parser a
withObject _ f (Object o) = f o
withObject name _ _ = Left (name ++ ": expected object")

(.:?) :: FromJSON a => Object -> Key -> Parser (Maybe a)
o .:? k =
  case KM.lookup k o of
    Nothing -> pure Nothing
    Just v -> Just <$> parseJSON v

(.!=) :: Parser (Maybe a) -> a -> Parser a
parser .!= fallback = do
  result <- parser
  pure (maybe fallback id result)

(.=) :: ToJSON a => Key -> a -> (Key, Value)
k .= v = (k, toJSON v)

instance FromJSON Value where
  parseJSON = pure

instance ToJSON Value where
  toJSON = id

instance FromJSON Text
instance ToJSON Text

instance {-# OVERLAPPING #-} FromJSON String
instance {-# OVERLAPPING #-} ToJSON String

instance FromJSON Bool
instance ToJSON Bool

instance FromJSON Int
instance ToJSON Int

instance FromJSON Integer
instance ToJSON Integer

instance FromJSON Int64
instance ToJSON Int64

instance FromJSON Double
instance ToJSON Double

instance FromJSON Natural
instance ToJSON Natural

instance FromJSON a => FromJSON (Maybe a)
instance ToJSON a => ToJSON (Maybe a)

instance FromJSON a => FromJSON [a]
instance ToJSON a => ToJSON [a]

instance (Ord k, FromJSON a) => FromJSON (Map k a)
instance ToJSON a => ToJSON (Map k a)
