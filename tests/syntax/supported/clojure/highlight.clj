;; Namespace and requires
(ns example.core
  (:require [clojure.string :as str]))

(def ^:const pi 3.14159)

(defn area
  "Compute circle area."
  [r]
  (* pi r r))

(defn classify [n]
  (cond
    (< n 0) :negative
    (zero? n) :zero
    :else :positive))

(let [xs [1 2 3 4]
      ys (map inc xs)]
  (when (seq ys)
    (println "sum" (reduce + 0 ys) \newline true nil)))
