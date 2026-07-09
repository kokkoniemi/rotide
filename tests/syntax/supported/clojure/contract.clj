;; Comprehensive Clojure fixture exercising the highlight query.
(ns example.contract
  (:require [clojure.string :as str]
            [clojure.set :as set])
  (:import (java.time Instant)))

(def ^:private version "1.2.3")
(def limits {:min 0 :max 100})
(declare process)

(defmacro unless [test & body]
  `(if (not ~test)
     (do ~@body)))

(defrecord Point [x y])

(defn distance
  "Euclidean distance between two points."
  [{x1 :x y1 :y} {x2 :x y2 :y}]
  (let [dx (- x2 x1)
        dy (- y2 y1)]
    (Math/sqrt (+ (* dx dx) (* dy dy)))))

(defmulti describe :kind)
(defmethod describe :circle [shape]
  (str "circle r=" (:radius shape)))
(defmethod describe :default [_]
  "unknown")

(defn process [items]
  (->> items
       (filter even?)
       (map #(* % %))
       (reduce + 0)))

(defn -main [& args]
  (doseq [n (range 5)]
    (when-not (neg? n)
      (println (format "n=%d sq=%d" n (* n n)))))
  (let [chars [\a \b \c]
        flags #{:read :write}]
    (cond-> {:ok true}
      (seq args) (assoc :args (vec args))
      (contains? flags :read) (assoc :readable true))))

(comment
  (process [1 2 3 4 5 6])
  (distance (->Point 0 0) (->Point 3 4)))
